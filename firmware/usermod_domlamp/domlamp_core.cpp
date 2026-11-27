// Domlamp core, see domlamp_core.h. No Arduino, no WLED, no heap.
#include "domlamp_core.h"

#include <string.h>

namespace domlamp {

const char *const ZONE_NAMES[ZONE_COUNT] = {"voet", "eerste_vierkant", "omloop1", "tweede_vierkant",
                                            "omloop2", "lantaarn", "dak"};

// ---- a small JSON reader for this one document ----------------------------------------------------------

namespace {

struct Cursor {
  const char *p;
  const char *end;
};

constexpr int MAX_DEPTH = 12;

void skipSpace(Cursor &c) {
  while (c.p < c.end && (*c.p == ' ' || *c.p == '\n' || *c.p == '\r' || *c.p == '\t')) ++c.p;
}

bool take(Cursor &c, char ch) {
  skipSpace(c);
  if (c.p < c.end && *c.p == ch) {
    ++c.p;
    return true;
  }
  return false;
}

// A string; copied to `out` (cut to fit) when out is not null.
bool readString(Cursor &c, char *out, size_t cap) {
  if (!take(c, '"')) return false;
  size_t n = 0;
  while (c.p < c.end) {
    char ch = *c.p++;
    if (ch == '"') {
      if (out && cap) out[n < cap ? n : cap - 1] = 0;
      return true;
    }
    if (ch == '\\') {
      if (c.p >= c.end) return false;
      char esc = *c.p++;
      switch (esc) {
        case 'n': ch = '\n'; break;
        case 't': ch = '\t'; break;
        case 'r': ch = '\r'; break;
        case 'b': ch = '\b'; break;
        case 'f': ch = '\f'; break;
        case 'u':
          if (c.end - c.p < 4) return false;
          c.p += 4;
          ch = '?';   // names in this document are ASCII; anything else is only shown, never compared
          break;
        default: ch = esc; break;   // \" \\ \/
      }
    }
    if (out && n + 1 < cap) out[n] = ch;
    ++n;
  }
  return false;
}

// A non-negative whole number that fits in 32 bits.
bool readU32(Cursor &c, uint32_t &out) {
  skipSpace(c);
  if (c.p >= c.end || *c.p < '0' || *c.p > '9') return false;
  uint64_t v = 0;
  while (c.p < c.end && *c.p >= '0' && *c.p <= '9') {
    v = v * 10 + (uint64_t)(*c.p - '0');
    if (v > 0xFFFFFFFFULL) return false;
    ++c.p;
  }
  if (c.p < c.end && (*c.p == '.' || *c.p == 'e' || *c.p == 'E')) return false;
  out = (uint32_t)v;
  return true;
}

bool skipValue(Cursor &c, int depth);

// Walks the members of an array or object: true while another one follows.
enum class Step { More, End, Bad };
Step nextMember(Cursor &c, bool &first, char close) {
  if (take(c, close)) return Step::End;
  if (!first && !take(c, ',')) return Step::Bad;
  first = false;
  return Step::More;
}

bool skipValue(Cursor &c, int depth) {
  if (depth > MAX_DEPTH) return false;
  skipSpace(c);
  if (c.p >= c.end) return false;
  char ch = *c.p;
  if (ch == '"') return readString(c, nullptr, 0);
  if (ch == '{' || ch == '[') {
    const char close = ch == '{' ? '}' : ']';
    ++c.p;
    bool first = true;
    for (;;) {
      Step s = nextMember(c, first, close);
      if (s == Step::End) return true;
      if (s == Step::Bad) return false;
      if (close == '}' && !(readString(c, nullptr, 0) && take(c, ':'))) return false;
      if (!skipValue(c, depth + 1)) return false;
    }
  }
  // number, true, false, null
  const char *start = c.p;
  while (c.p < c.end && *c.p != ',' && *c.p != '}' && *c.p != ']' && *c.p != ' ' && *c.p != '\n' &&
         *c.p != '\r' && *c.p != '\t')
    ++c.p;
  return c.p > start;
}

// [[r,g,b,w,strength] x 7]
bool readZones(Cursor &c, Zone zones[ZONE_COUNT]) {
  if (!take(c, '[')) return false;
  for (int z = 0; z < ZONE_COUNT; z++) {
    if (z > 0 && !take(c, ',')) return false;
    if (!take(c, '[')) return false;
    uint8_t v[5];
    for (int k = 0; k < 5; k++) {
      uint32_t n;
      if (k > 0 && !take(c, ',')) return false;
      if (!readU32(c, n) || n > 255) return false;
      v[k] = (uint8_t)n;
    }
    if (!take(c, ']')) return false;
    zones[z] = Zone{v[0], v[1], v[2], v[3], v[4]};
  }
  return take(c, ']');
}

// ["voet", ...]: exactly the names this firmware maps to its segments
bool readZoneNames(Cursor &c, bool &match) {
  if (!take(c, '[')) return false;
  bool first = true;
  int n = 0;
  match = true;
  for (;;) {
    Step s = nextMember(c, first, ']');
    if (s == Step::Bad) return false;
    if (s == Step::End) break;
    char name[24];
    if (!readString(c, name, sizeof(name))) return false;
    if (n >= ZONE_COUNT || strcmp(name, ZONE_NAMES[n]) != 0) match = false;
    n++;
  }
  if (n != ZONE_COUNT) match = false;
  return true;
}

Transition transitionFromName(const char *name) {
  if (strcmp(name, "kaars_aan") == 0) return Transition::CandleOn;
  if (strcmp(name, "kaars_uit") == 0) return Transition::CandleOff;
  return Transition::Fade;   // "vervagen" and anything a later server may invent
}

struct ObjectReader {
  Cursor &c;
  bool first = true;
  char key[16];
  explicit ObjectReader(Cursor &cur) : c(cur) { key[0] = 0; }
  // Step::More: `key` holds the next member's name and the cursor is at its value.
  Step next() {
    Step s = nextMember(c, first, '}');
    if (s != Step::More) return s;
    if (!readString(c, key, sizeof(key)) || !take(c, ':')) return Step::Bad;
    return Step::More;
  }
  bool is(const char *name) const { return strcmp(key, name) == 0; }
};

ParseError readCurrent(Cursor &c, Plan &plan, bool &have_zones) {
  if (!take(c, '{')) return ParseError::Syntax;
  ObjectReader obj(c);
  for (;;) {
    Step s = obj.next();
    if (s == Step::Bad) return ParseError::Syntax;
    if (s == Step::End) return ParseError::Ok;
    if (obj.is("scene")) {
      if (!readString(c, plan.now.name, sizeof(plan.now.name))) return ParseError::Syntax;
    } else if (obj.is("sinds")) {
      if (!readU32(c, plan.since)) return ParseError::Syntax;
    } else if (obj.is("zones")) {
      if (!readZones(c, plan.now.zones)) return ParseError::BadZones;
      have_zones = true;
    } else if (!skipValue(c, 1)) {
      return ParseError::Syntax;
    }
  }
}

ParseError readEntry(Cursor &c, Entry &entry) {
  if (!take(c, '{')) return ParseError::Syntax;
  bool have_at = false, have_zones = false;
  entry = Entry{};
  entry.duration_s = 5;
  ObjectReader obj(c);
  for (;;) {
    Step s = obj.next();
    if (s == Step::Bad) return ParseError::Syntax;
    if (s == Step::End) break;
    if (obj.is("om")) {
      if (!readU32(c, entry.at)) return ParseError::Syntax;
      have_at = true;
    } else if (obj.is("scene")) {
      if (!readString(c, entry.scene.name, sizeof(entry.scene.name))) return ParseError::Syntax;
    } else if (obj.is("overgang")) {
      char name[16];
      if (!readString(c, name, sizeof(name))) return ParseError::Syntax;
      entry.transition = transitionFromName(name);
    } else if (obj.is("duur_s")) {
      uint32_t n;
      if (!readU32(c, n)) return ParseError::Syntax;
      entry.duration_s = (uint16_t)(n > 600 ? 600 : n);
    } else if (obj.is("zones")) {
      if (!readZones(c, entry.scene.zones)) return ParseError::BadZones;
      have_zones = true;
    } else if (!skipValue(c, 1)) {
      return ParseError::Syntax;
    }
  }
  return have_at && have_zones ? ParseError::Ok : ParseError::BadEntry;
}

ParseError readEntries(Cursor &c, Plan &plan, uint32_t &first_dropped) {
  if (!take(c, '[')) return ParseError::Syntax;
  bool first = true;
  uint32_t previous = 0;
  for (;;) {
    Step s = nextMember(c, first, ']');
    if (s == Step::Bad) return ParseError::Syntax;
    if (s == Step::End) return ParseError::Ok;
    Entry entry;
    ParseError e = readEntry(c, entry);
    if (e != ParseError::Ok) return e;
    if (entry.at < previous) return ParseError::BadEntry;
    previous = entry.at;
    if (plan.count < MAX_ENTRIES) {
      plan.entries[plan.count++] = entry;
    } else if (!plan.truncated) {
      plan.truncated = true;
      first_dropped = entry.at;
    }
  }
}

ParseError parseInto(const char *json, size_t len, Plan &plan) {
  Cursor c{json, json + len};
  if (!take(c, '{')) return ParseError::Syntax;
  bool have_version = false, have_time = false, have_current = false, names_match = true;
  uint32_t version = 0, first_dropped = 0;
  plan.next_s = 60;
  ObjectReader obj(c);
  for (;;) {
    Step s = obj.next();
    if (s == Step::Bad) return ParseError::Syntax;
    if (s == Step::End) break;
    if (obj.is("versie")) {
      if (!readU32(c, version)) return ParseError::Syntax;
      have_version = true;
    } else if (obj.is("tijd")) {
      if (!readU32(c, plan.time)) return ParseError::Syntax;
      have_time = true;
    } else if (obj.is("volgende_s")) {
      uint32_t n;
      if (!readU32(c, n)) return ParseError::Syntax;
      plan.next_s = (uint16_t)(n > 0xFFFF ? 0xFFFF : n);
    } else if (obj.is("zones")) {
      if (!readZoneNames(c, names_match)) return ParseError::Syntax;
    } else if (obj.is("nu")) {
      ParseError e = readCurrent(c, plan, have_current);
      if (e != ParseError::Ok) return e;
    } else if (obj.is("plan")) {
      ParseError e = readEntries(c, plan, first_dropped);
      if (e != ParseError::Ok) return e;
    } else if (!skipValue(c, 1)) {
      return ParseError::Syntax;
    }
  }
  skipSpace(c);
  if (c.p != c.end) return ParseError::Syntax;
  if (!have_version || version != 1) return ParseError::Version;
  if (!have_time || plan.time < MIN_VALID_UNIX) return ParseError::NoTime;
  if (!names_match) return ParseError::ZoneNames;
  if (!have_current) return ParseError::NoCurrent;
  plan.until = plan.truncated ? first_dropped : plan.time + PLAN_HORIZON_S;
  return ParseError::Ok;
}

}  // namespace

const char *parseErrorText(ParseError e) {
  switch (e) {
    case ParseError::Ok: return "ok";
    case ParseError::Syntax: return "plan: not valid JSON";
    case ParseError::Version: return "plan: unknown version";
    case ParseError::NoTime: return "plan: no server time";
    case ParseError::ZoneNames: return "plan: other zones than this firmware knows";
    case ParseError::NoCurrent: return "plan: no current scene";
    case ParseError::BadZones: return "plan: bad zone values";
    case ParseError::BadEntry: return "plan: bad entry";
  }
  return "plan: error";
}

ParseError parsePlan(const char *json, size_t len, Plan &out) {
  out = Plan{};
  ParseError e = parseInto(json, len, out);
  out.valid = e == ParseError::Ok;
  return e;
}

// ---- following the plan --------------------------------------------------------------------------------

int activeIndex(const Plan &plan, uint32_t now) {
  int index = -1;
  for (int i = 0; i < plan.count; i++) {
    if (plan.entries[i].at <= now) index = i;
    else break;
  }
  return index;
}

const Scene &sceneAt(const Plan &plan, int index) {
  return index < 0 || index >= plan.count ? plan.now : plan.entries[index].scene;
}

bool planExpired(const Plan &plan, uint32_t now) { return !plan.valid || now >= plan.until; }

const Entry *nextEntry(const Plan &plan, uint32_t now) {
  if (!plan.valid) return nullptr;
  for (int i = 0; i < plan.count; i++)
    if (plan.entries[i].at > now) return &plan.entries[i];
  return nullptr;
}

bool sameZones(const Scene &a, const Scene &b) {
  for (int z = 0; z < ZONE_COUNT; z++) {
    const Zone &p = a.zones[z], &q = b.zones[z];
    if (p.r != q.r || p.g != q.g || p.b != q.b || p.w != q.w || p.level != q.level) return false;
  }
  return true;
}

// ---- fallback schedule ---------------------------------------------------------------------------------

int parseClockTime(const char *text) {
  if (!text) return -1;
  int part[2] = {0, 0}, digits[2] = {0, 0}, k = 0;
  for (const char *p = text; *p; ++p) {
    if (*p >= '0' && *p <= '9') {
      part[k] = part[k] * 10 + (*p - '0');
      if (++digits[k] > 2) return -1;
    } else if (*p == ':' && k == 0) {
      k = 1;
    } else {
      return -1;
    }
  }
  if (k != 1 || digits[0] == 0 || digits[1] != 2 || part[0] > 23 || part[1] > 59) return -1;
  return part[0] * 60 + part[1];
}

bool fallbackIsOn(int on_min, int off_min, int minute_of_day) {
  if (on_min == off_min) return true;
  if (on_min < off_min) return minute_of_day >= on_min && minute_of_day < off_min;
  return minute_of_day >= on_min || minute_of_day < off_min;   // across midnight
}

Scene fallbackScene(bool on, uint8_t level) {
  Scene s = {};
  strcpy(s.name, on ? "fallback" : "fallback off");
  for (int z = 0; z < ZONE_COUNT; z++) s.zones[z] = on ? Zone{0, 0, 0, 255, level} : Zone{0, 0, 0, 0, 0};
  return s;
}

// ---- deciding what to show -----------------------------------------------------------------------------

bool Director::update(const Plan *plan, const Inputs &in, Change &out) {
  Change next = {};
  next.transition = Transition::Fade;
  next.duration_ms = QUICK_FADE_MS;

  const bool have_clock = in.now >= MIN_VALID_UNIX;
  if (plan && plan->valid && have_clock && !planExpired(*plan, in.now)) {
    const int index = activeIndex(*plan, in.now);
    next.source = Source::Plan;
    next.target = sceneAt(*plan, index);
    if (index >= 0) {
      const Entry &entry = plan->entries[index];
      if (in.now - entry.at <= FRESH_ENTRY_S) {     // reached just now: move the way the plan says
        next.transition = entry.transition;
        next.duration_ms = (uint32_t)entry.duration_s * 1000UL;
      }
    }
  } else {
    // no plan for this moment: warm white inside the fixed window; without a clock simply on
    const bool on = in.minute_of_day < 0 || !have_clock ||
                    fallbackIsOn(in.fallback_on_min, in.fallback_off_min, in.minute_of_day);
    next.source = Source::Fallback;
    next.target = fallbackScene(on, in.fallback_level);
  }

  const bool changed = !has_shown_ || !sameZones(shown_, next.target);
  shown_ = next.target;      // the name may change while the light stays the same
  source_ = next.source;
  has_shown_ = true;
  if (changed) out = next;
  return changed;
}

// ---- transitions ---------------------------------------------------------------------------------------

void schedule(Transition transition, uint32_t duration_ms, ZoneStep out[ZONE_COUNT]) {
  if (transition == Transition::Fade) {
    for (int z = 0; z < ZONE_COUNT; z++) out[z] = ZoneStep{0, duration_ms};
    return;
  }
  const uint32_t step = duration_ms / ZONE_COUNT;
  for (int z = 0; z < ZONE_COUNT; z++) {
    // place in the row: the top zone first when lighting, the foot first when going out
    const uint32_t k = transition == Transition::CandleOn ? (uint32_t)(ZONE_COUNT - 1 - z) : (uint32_t)z;
    out[z] = ZoneStep{k * step, step};
  }
}

float progress(const ZoneStep &step, uint32_t t_ms) {
  if (t_ms < step.delay_ms) return 0.0f;
  const uint32_t into = t_ms - step.delay_ms;
  if (into >= step.duration_ms) return 1.0f;     // also a zone without a fade of its own
  return (float)into / (float)step.duration_ms;
}

// ---- polling -------------------------------------------------------------------------------------------

uint32_t pollDelay(uint32_t base_s, uint8_t failures) {
  const uint32_t cap = 900;
  if (base_s < 15) base_s = 15;
  if (base_s >= cap) return base_s;     // a longer interval chosen on purpose stays as it is
  uint32_t delay = base_s;
  for (uint8_t i = 0; i < failures && delay < cap; i++) delay *= 2;
  return delay > cap ? cap : delay;
}

bool parseUrl(const char *text, Url &out) {
  out = Url{};
  if (!text) return false;
  if (strncmp(text, "https://", 8) == 0) {
    out.tls = true;
    out.port = 443;
    text += 8;
  } else if (strncmp(text, "http://", 7) == 0) {
    out.tls = false;
    out.port = 80;
    text += 7;
  } else {
    return false;
  }
  size_t n = 0;
  while (*text && *text != '/' && *text != ':' && *text != '?' && *text != '#') {
    if (n + 1 >= sizeof(out.host) || *text == ' ' || *text == '@') return false;
    out.host[n++] = *text++;
  }
  out.host[n] = 0;
  if (n == 0) return false;
  if (*text == ':') {
    ++text;
    uint32_t port = 0;
    int digits = 0;
    while (*text >= '0' && *text <= '9') {
      port = port * 10 + (uint32_t)(*text++ - '0');
      if (++digits > 5) return false;
    }
    if (digits == 0 || port == 0 || port > 65535) return false;
    out.port = (uint16_t)port;
  }
  if (*text == 0) {
    strcpy(out.path, "/");
    return true;
  }
  if (*text != '/') return false;
  n = 0;
  while (*text && *text != '#') {
    if (n + 1 >= sizeof(out.path) || *text == ' ' || *text == '\r' || *text == '\n') return false;
    out.path[n++] = *text++;
  }
  out.path[n] = 0;
  return true;
}

namespace {

char lower(char ch) { return ch >= 'A' && ch <= 'Z' ? (char)(ch + 32) : ch; }

// The value of a header (name in lower case, with the colon) inside [head, head_end), or nullptr.
const char *findHeader(const char *head, const char *head_end, const char *name, const char *&value_end) {
  const size_t name_len = strlen(name);
  const char *line = head;
  while (line < head_end) {
    const char *eol = line;
    while (eol < head_end && *eol != '\r' && *eol != '\n') ++eol;
    if ((size_t)(eol - line) >= name_len) {
      size_t i = 0;
      while (i < name_len && lower(line[i]) == name[i]) ++i;
      if (i == name_len) {
        const char *v = line + name_len;
        while (v < eol && (*v == ' ' || *v == '\t')) ++v;
        value_end = eol;
        return v;
      }
    }
    line = eol;
    while (line < head_end && (*line == '\r' || *line == '\n')) ++line;
  }
  return nullptr;
}

bool containsLower(const char *p, const char *end, const char *word) {
  const size_t n = strlen(word);
  for (; p + n <= end; ++p) {
    size_t i = 0;
    while (i < n && lower(p[i]) == word[i]) ++i;
    if (i == n) return true;
  }
  return false;
}

int hexValue(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  ch = lower(ch);
  return ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
}

// Walks the chunks from `body`; with `write` set they are joined at the start of the body.
HttpState walkChunks(char *body, char *end, bool write, size_t &joined) {
  char *p = body, *dst = body;
  for (;;) {
    size_t size = 0;
    int digits = 0;
    while (p < end && hexValue(*p) >= 0) {
      size = size * 16 + (size_t)hexValue(*p++);
      if (++digits > 6) return HttpState::Bad;
    }
    if (p >= end) return HttpState::Incomplete;
    if (digits == 0) return HttpState::Bad;
    while (p < end && *p != '\n') ++p;          // chunk extensions, CR
    if (p >= end) return HttpState::Incomplete;
    ++p;
    if (size == 0) {
      joined = (size_t)(dst - body);
      return HttpState::Complete;               // trailers, if any, are not needed
    }
    if ((size_t)(end - p) < size) return HttpState::Incomplete;
    if (write) memmove(dst, p, size);
    dst += size;
    p += size;
    if (p < end && *p == '\r') ++p;
    if (p >= end) return HttpState::Incomplete;
    if (*p != '\n') return HttpState::Bad;
    ++p;
  }
}

}  // namespace

HttpState parseHttpResponse(char *buf, size_t len, bool eof, HttpResponse &out) {
  out = HttpResponse{0, nullptr, 0};
  char *end = buf + len;
  char *head_end = nullptr;
  for (char *p = buf; p + 3 < end; ++p) {
    if (p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n') {
      head_end = p;
      break;
    }
  }
  if (!head_end) return eof || len > 4096 ? HttpState::Bad : HttpState::Incomplete;
  if (len < 12 || strncmp(buf, "HTTP/1.", 7) != 0 || buf[8] != ' ') return HttpState::Bad;
  int status = 0;
  for (int i = 9; i < 12; i++) {
    if (buf[i] < '0' || buf[i] > '9') return HttpState::Bad;
    status = status * 10 + (buf[i] - '0');
  }
  out.status = status;
  char *body = head_end + 4;

  const char *value_end = nullptr;
  const char *value = findHeader(buf, head_end, "transfer-encoding:", value_end);
  if (value && containsLower(value, value_end, "chunked")) {
    size_t joined = 0;
    HttpState s = walkChunks(body, end, false, joined);
    if (s == HttpState::Incomplete) return eof ? HttpState::Bad : HttpState::Incomplete;
    if (s == HttpState::Bad) return s;
    walkChunks(body, end, true, joined);
    out.body = body;
    out.body_len = joined;
    body[joined] = 0;
    return HttpState::Complete;
  }

  value = findHeader(buf, head_end, "content-length:", value_end);
  if (value) {
    size_t length = 0;
    int digits = 0;
    for (const char *p = value; p < value_end && *p >= '0' && *p <= '9'; ++p) {
      length = length * 10 + (size_t)(*p - '0');
      if (++digits > 7) return HttpState::Bad;
    }
    if (digits == 0) return HttpState::Bad;
    if ((size_t)(end - body) < length) return eof ? HttpState::Bad : HttpState::Incomplete;
    out.body = body;
    out.body_len = length;
    body[length] = 0;
    return HttpState::Complete;
  }

  if (!eof) return HttpState::Incomplete;       // no length given: the body ends when the server closes
  out.body = body;
  out.body_len = (size_t)(end - body);
  *end = 0;
  return HttpState::Complete;
}

// ---- persistence ---------------------------------------------------------------------------------------

uint32_t crc32(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
  }
  return ~crc;
}

namespace {

constexpr uint32_t PACK_MAGIC = 0x31504C44UL;   // "DLP1"

struct Writer {
  uint8_t *p;
  void u8(uint8_t v) { *p++ = v; }
  void u16(uint16_t v) { u8((uint8_t)v); u8((uint8_t)(v >> 8)); }
  void u32(uint32_t v) { u16((uint16_t)v); u16((uint16_t)(v >> 16)); }
  void scene(const Scene &s) {
    memcpy(p, s.name, SCENE_NAME_LEN);
    p += SCENE_NAME_LEN;
    for (int z = 0; z < ZONE_COUNT; z++) {
      const Zone &q = s.zones[z];
      u8(q.r); u8(q.g); u8(q.b); u8(q.w); u8(q.level);
    }
  }
};

struct Reader {
  const uint8_t *p;
  uint8_t u8() { return *p++; }
  uint16_t u16() { uint16_t lo = u8(); return (uint16_t)(lo | (uint16_t)(u8() << 8)); }
  uint32_t u32() { uint32_t lo = u16(); return lo | ((uint32_t)u16() << 16); }
  void scene(Scene &s) {
    memcpy(s.name, p, SCENE_NAME_LEN);
    s.name[SCENE_NAME_LEN - 1] = 0;
    p += SCENE_NAME_LEN;
    for (int z = 0; z < ZONE_COUNT; z++) {
      Zone &q = s.zones[z];
      q.r = u8(); q.g = u8(); q.b = u8(); q.w = u8(); q.level = u8();
    }
  }
};

constexpr size_t PACK_HEADER = 12;   // magic, payload size, crc of the payload
constexpr size_t PACK_SCENE = SCENE_NAME_LEN + ZONE_COUNT * 5;
constexpr size_t PACK_FIXED = 4 + 4 + 4 + 2 + 1 + 1 + PACK_SCENE;
constexpr size_t PACK_ENTRY = 4 + 1 + 2 + PACK_SCENE;

}  // namespace

size_t packPlan(const Plan &plan, uint8_t *buf, size_t cap) {
  if (!plan.valid || plan.count > MAX_ENTRIES) return 0;
  const size_t payload = PACK_FIXED + (size_t)plan.count * PACK_ENTRY;
  if (cap < PACK_HEADER + payload) return 0;
  Writer w{buf + PACK_HEADER};
  w.u32(plan.time);
  w.u32(plan.until);
  w.u32(plan.since);
  w.u16(plan.next_s);
  w.u8(plan.truncated ? 1 : 0);
  w.u8(plan.count);
  w.scene(plan.now);
  for (int i = 0; i < plan.count; i++) {
    const Entry &e = plan.entries[i];
    w.u32(e.at);
    w.u8((uint8_t)e.transition);
    w.u16(e.duration_s);
    w.scene(e.scene);
  }
  Writer h{buf};
  h.u32(PACK_MAGIC);
  h.u32((uint32_t)payload);
  h.u32(crc32(buf + PACK_HEADER, payload));
  return PACK_HEADER + payload;
}

bool unpackPlan(const uint8_t *buf, size_t len, Plan &out) {
  out = Plan{};
  if (len < PACK_HEADER + PACK_FIXED) return false;
  Reader h{buf};
  if (h.u32() != PACK_MAGIC) return false;
  const uint32_t payload = h.u32();
  const uint32_t crc = h.u32();
  if (payload != len - PACK_HEADER || crc32(buf + PACK_HEADER, payload) != crc) return false;
  Reader r{buf + PACK_HEADER};
  out.time = r.u32();
  out.until = r.u32();
  out.since = r.u32();
  out.next_s = r.u16();
  out.truncated = r.u8() != 0;
  out.count = r.u8();
  if (out.count > MAX_ENTRIES || payload != PACK_FIXED + (size_t)out.count * PACK_ENTRY) {
    out = Plan{};
    return false;
  }
  r.scene(out.now);
  for (int i = 0; i < out.count; i++) {
    Entry &e = out.entries[i];
    e.at = r.u32();
    const uint8_t t = r.u8();
    e.transition = t <= (uint8_t)Transition::CandleOff ? (Transition)t : Transition::Fade;
    e.duration_s = r.u16();
    r.scene(e.scene);
  }
  out.valid = true;
  return true;
}

// ---- link ---------------------------------------------------------------------------------------------

bool parseLinkLine(const char *line, size_t len, Zone out[ZONE_COUNT]) {
  if (len > 0 && line[len - 1] == '\r') len--;
  if (len != LINK_LINE_LEN || line[0] != 'Z') return false;
  uint8_t bytes[ZONE_COUNT * 5];
  for (int k = 0; k < ZONE_COUNT * 5; k++) {
    const int hi = hexValue(line[1 + 2 * k]), lo = hexValue(line[2 + 2 * k]);
    if (hi < 0 || lo < 0) return false;
    bytes[k] = (uint8_t)(hi * 16 + lo);
  }
  for (int z = 0; z < ZONE_COUNT; z++) {
    out[z] = Zone{bytes[5 * z], bytes[5 * z + 1], bytes[5 * z + 2], bytes[5 * z + 3], bytes[5 * z + 4]};
  }
  return true;
}

bool validLinkCode(const char *text) {
  if (!text) return false;
  int n = 0;
  for (; text[n]; n++) {
    const char c = text[n];
    const bool letter = c >= 'A' && c <= 'Z' && c != 'I' && c != 'O';
    const bool digit = c >= '2' && c <= '9';
    if (!letter && !digit) return false;
  }
  return n == LINK_CODE_LEN;
}

}  // namespace domlamp
