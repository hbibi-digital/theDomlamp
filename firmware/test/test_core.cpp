// Host test for the Domlamp core (usermod_domlamp/domlamp_core.cpp). No hardware, no WLED.
// Build and run: ./run.sh
#include "../usermod_domlamp/domlamp_core.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace domlamp;

static int checks = 0, failures = 0;

#define CHECK(cond)                                                              \
  do {                                                                           \
    ++checks;                                                                    \
    if (!(cond)) {                                                               \
      ++failures;                                                                \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
    }                                                                            \
  } while (0)

static std::string readFile(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static ParseError parse(const std::string &json, Plan &plan) { return parsePlan(json.data(), json.size(), plan); }

static bool zoneIs(const Zone &z, int r, int g, int b, int w, int level) {
  return z.r == r && z.g == g && z.b == b && z.w == w && z.level == level;
}

// A small valid document to cut and bend.
static std::string zonesJson(int w, int level) {
  std::string s = "[";
  for (int z = 0; z < ZONE_COUNT; z++) {
    if (z) s += ",";
    s += "[0,0,0," + std::to_string(w) + "," + std::to_string(level) + "]";
  }
  return s + "]";
}
static std::string entryJson(uint32_t at, const char *scene, const char *transition, int duration, int level) {
  return std::string("{\"om\":") + std::to_string(at) + ",\"scene\":\"" + scene + "\",\"overgang\":\"" + transition +
         "\",\"duur_s\":" + std::to_string(duration) + ",\"zones\":" + zonesJson(255, level) + "}";
}
static const uint32_t T0 = 1795629048UL;
static std::string docJson(const std::string &entries, const char *version = "1") {
  return std::string("{\"versie\":") + version + ",\"tijd\":" + std::to_string(T0) +
         ",\"volgende_s\":60,\"zones\":[\"voet\",\"eerste_vierkant\",\"omloop1\",\"tweede_vierkant\",\"omloop2\","
         "\"lantaarn\",\"dak\"],\"nu\":{\"scene\":\"warmwit\",\"sinds\":1795621560,\"zones\":" +
         zonesJson(255, 200) + "},\"plan\":[" + entries + "]}";
}

static void testParseSample(const std::string &dir) {
  const std::string json = readFile(dir + "/licht-sample.json");
  CHECK(!json.empty());
  Plan plan;
  CHECK(parse(json, plan) == ParseError::Ok);
  CHECK(plan.valid && !plan.truncated);
  CHECK(plan.time == 1795629048UL);
  CHECK(plan.until == 1795629048UL + 36UL * 3600UL);
  CHECK(plan.since == 1795621560UL);
  CHECK(plan.next_s == 60);
  CHECK(std::strcmp(plan.now.name, "warmwit") == 0);
  CHECK(zoneIs(plan.now.zones[0], 0, 0, 0, 255, 255));
  CHECK(zoneIs(plan.now.zones[5], 90, 52, 0, 255, 255));
  CHECK(zoneIs(plan.now.zones[6], 136, 79, 0, 255, 255));
  CHECK(plan.count == 8);
  CHECK(plan.entries[0].at == 1795644000UL);
  CHECK(std::strcmp(plan.entries[0].scene.name, "na23_glas") == 0);
  CHECK(plan.entries[0].transition == Transition::Fade && plan.entries[0].duration_s == 5);
  CHECK(zoneIs(plan.entries[0].scene.zones[0], 108, 62, 0, 255, 89));
  CHECK(zoneIs(plan.entries[0].scene.zones[1], 0, 0, 0, 255, 102));
  CHECK(zoneIs(plan.entries[0].scene.zones[2], 0, 0, 0, 0, 0));
  CHECK(plan.entries[3].transition == Transition::CandleOff && plan.entries[3].duration_s == 30);
  CHECK(std::strcmp(plan.entries[3].scene.name, "dag") == 0);
  CHECK(plan.entries[4].transition == Transition::CandleOn && plan.entries[4].duration_s == 30);
  CHECK(plan.entries[7].at == 1795752000UL);

  // which scene is in force when
  CHECK(activeIndex(plan, plan.time) == -1);
  CHECK(activeIndex(plan, 1795644000UL - 1) == -1);
  CHECK(activeIndex(plan, 1795644000UL) == 0);
  CHECK(activeIndex(plan, 1795647600UL + 10) == 1);
  CHECK(activeIndex(plan, 1795676940UL) == 3);
  CHECK(activeIndex(plan, 1795999999UL) == 7);
  CHECK(std::strcmp(sceneAt(plan, -1).name, "warmwit") == 0);
  CHECK(std::strcmp(sceneAt(plan, 3).name, "dag") == 0);
  CHECK(!planExpired(plan, plan.time));
  CHECK(!planExpired(plan, plan.until - 1));
  CHECK(planExpired(plan, plan.until));
  CHECK(nextEntry(plan, plan.time) == &plan.entries[0]);
  CHECK(nextEntry(plan, 1795644000UL) == &plan.entries[1]);
  CHECK(nextEntry(plan, 1795752000UL) == nullptr);
}

static void testParseErrors() {
  Plan plan;
  CHECK(parse(docJson(""), plan) == ParseError::Ok && plan.count == 0 && plan.valid);
  CHECK(parse("", plan) == ParseError::Syntax && !plan.valid);
  CHECK(parse("<html>502 Bad Gateway</html>", plan) == ParseError::Syntax);
  CHECK(parse("[]", plan) == ParseError::Syntax);
  CHECK(parse("{}", plan) == ParseError::Version);
  CHECK(parse(docJson("", "2"), plan) == ParseError::Version && !plan.valid);
  CHECK(parse(docJson("") + "x", plan) == ParseError::Syntax);

  // cut off at every length: never accepted, never a crash
  const std::string whole = docJson(entryJson(T0 + 600, "dag", "kaars_uit", 30, 0));
  bool any_accepted = false;
  for (size_t n = 0; n < whole.size(); n++) {
    if (parsePlan(whole.data(), n, plan) == ParseError::Ok || plan.valid) any_accepted = true;
  }
  CHECK(!any_accepted);
  CHECK(parse(whole, plan) == ParseError::Ok && plan.count == 1);

  // unknown members are skipped, also nested ones
  std::string extra = whole;
  extra.insert(1, "\"nieuw\":{\"a\":[1,2,{\"b\":null}],\"c\":\"x\\\"y\",\"d\":-1.5e3,\"e\":true},");
  CHECK(parse(extra, plan) == ParseError::Ok && plan.count == 1);
  // also inside "nu": the occasion behind a coloured scene, a name or null
  for (const char *why : {"\"aanleiding\":\"Orange the World\",", "\"aanleiding\":null,"}) {
    std::string occasion = whole;
    occasion.insert(occasion.find("\"sinds\""), why);
    CHECK(parse(occasion, plan) == ParseError::Ok && plan.count == 1);
  }

  // zones
  std::string names = whole;
  names.replace(names.find("\"voet\""), 6, "\"kelder\"");
  CHECK(parse(names, plan) == ParseError::ZoneNames);
  std::string six = docJson("");
  six.replace(six.find(",[0,0,0,255,200]]"), 17, "]");      // one zone short in "nu"
  CHECK(parse(six, plan) == ParseError::BadZones);
  std::string big = docJson("");
  big.replace(big.find("[0,0,0,255,200]"), 15, "[0,0,0,256,200]");
  CHECK(parse(big, plan) == ParseError::BadZones);
  std::string negative = docJson("");
  negative.replace(negative.find("[0,0,0,255,200]"), 15, "[0,0,0,-1,200]");
  CHECK(parse(negative, plan) == ParseError::BadZones);
  std::string no_now = docJson("");
  no_now.replace(no_now.find("\"nu\""), 4, "\"was\"");
  CHECK(parse(no_now, plan) == ParseError::NoCurrent);
  std::string no_time = docJson("");
  no_time.replace(no_time.find("\"tijd\""), 6, "\"klok\"");
  CHECK(parse(no_time, plan) == ParseError::NoTime);

  // entries
  CHECK(parse(docJson("{\"scene\":\"dag\",\"zones\":" + zonesJson(0, 0) + "}"), plan) == ParseError::BadEntry);
  CHECK(parse(docJson("{\"om\":" + std::to_string(T0 + 60) + "}"), plan) == ParseError::BadEntry);
  CHECK(parse(docJson(entryJson(T0 + 600, "a", "vervagen", 5, 1) + "," + entryJson(T0 + 300, "b", "vervagen", 5, 2)),
              plan) == ParseError::BadEntry);                // not ascending
  CHECK(parse(docJson(entryJson(T0 + 600, "a", "regenboog", 5, 1)), plan) == ParseError::Ok);
  CHECK(plan.entries[0].transition == Transition::Fade);     // an unknown transition is a plain fade
  CHECK(parse(docJson(entryJson(T0 + 600, "een_veel_te_lange_scenenaam_van_de_server", "vervagen", 99999, 1)),
              plan) == ParseError::Ok);
  CHECK(std::strlen(plan.entries[0].scene.name) == SCENE_NAME_LEN - 1);
  CHECK(plan.entries[0].duration_s == 600);

  // more changes than fit: keep the first ones and stop trusting the plan where the dropped ones begin
  std::string many;
  for (int i = 0; i < MAX_ENTRIES + 3; i++) {
    if (i) many += ",";
    many += entryJson(T0 + 600 * (uint32_t)(i + 1), "s", "vervagen", 5, i);
  }
  CHECK(parse(docJson(many), plan) == ParseError::Ok);
  CHECK(plan.count == MAX_ENTRIES && plan.truncated);
  CHECK(plan.until == T0 + 600 * (uint32_t)(MAX_ENTRIES + 1));
  CHECK(!planExpired(plan, plan.until - 1) && planExpired(plan, plan.until));
}

static void testFallback() {
  CHECK(parseClockTime("17:00") == 17 * 60);
  CHECK(parseClockTime("7:05") == 7 * 60 + 5);
  CHECK(parseClockTime("00:00") == 0);
  CHECK(parseClockTime("23:59") == 23 * 60 + 59);
  CHECK(parseClockTime("24:00") == -1);
  CHECK(parseClockTime("12:60") == -1);
  CHECK(parseClockTime("12") == -1);
  CHECK(parseClockTime("12:5") == -1);
  CHECK(parseClockTime("") == -1);
  CHECK(parseClockTime("ab:cd") == -1);
  CHECK(parseClockTime(nullptr) == -1);

  const int on = 17 * 60, off = 23 * 60;
  CHECK(!fallbackIsOn(on, off, on - 1));
  CHECK(fallbackIsOn(on, off, on));
  CHECK(fallbackIsOn(on, off, off - 1));
  CHECK(!fallbackIsOn(on, off, off));
  CHECK(!fallbackIsOn(on, off, 3 * 60));
  // across midnight: 22:00 to 01:30
  CHECK(fallbackIsOn(22 * 60, 90, 23 * 60));
  CHECK(fallbackIsOn(22 * 60, 90, 0));
  CHECK(fallbackIsOn(22 * 60, 90, 89));
  CHECK(!fallbackIsOn(22 * 60, 90, 90));
  CHECK(!fallbackIsOn(22 * 60, 90, 12 * 60));
  CHECK(fallbackIsOn(600, 600, 0) && fallbackIsOn(600, 600, 600));   // equal times: always on

  Scene s = fallbackScene(true, 180);
  for (int z = 0; z < ZONE_COUNT; z++) CHECK(zoneIs(s.zones[z], 0, 0, 0, 255, 180));
  s = fallbackScene(false, 180);
  for (int z = 0; z < ZONE_COUNT; z++) CHECK(zoneIs(s.zones[z], 0, 0, 0, 0, 0));
}

static Inputs inputs(uint32_t now, int minute_of_day) { return Inputs{now, minute_of_day, 17 * 60, 23 * 60, 200}; }

static void testDirector(const std::string &dir) {
  Plan plan;
  CHECK(parse(readFile(dir + "/licht-sample.json"), plan) == ParseError::Ok);
  Director d;
  Change c;

  // first look: the current scene, with the quick fade
  CHECK(d.update(&plan, inputs(plan.time, 600), c));
  CHECK(c.source == Source::Plan && std::strcmp(c.target.name, "warmwit") == 0);
  CHECK(c.transition == Transition::Fade && c.duration_ms == QUICK_FADE_MS);
  // nothing changes: no report, minute after minute
  for (uint32_t t = plan.time + 1; t < 1795644000UL; t += 61) CHECK(!d.update(&plan, inputs(t, 600), c));
  // the first change arrives: its own transition
  CHECK(d.update(&plan, inputs(1795644000UL, 600), c));
  CHECK(std::strcmp(c.target.name, "na23_glas") == 0 && c.transition == Transition::Fade && c.duration_ms == 5000);
  CHECK(std::strcmp(d.sceneName(), "na23_glas") == 0 && d.source() == Source::Plan);
  CHECK(!d.update(&plan, inputs(1795644001UL, 600), c));
  // the candle going out in the morning, noticed two seconds late
  CHECK(d.update(&plan, inputs(1795647600UL, 600), c));
  CHECK(d.update(&plan, inputs(1795665600UL, 600), c));
  CHECK(d.update(&plan, inputs(1795676942UL, 600), c));
  CHECK(std::strcmp(c.target.name, "dag") == 0 && c.transition == Transition::CandleOff && c.duration_ms == 30000);
  // and lighting in the evening
  CHECK(d.update(&plan, inputs(1795707900UL, 600), c));
  CHECK(c.transition == Transition::CandleOn && c.duration_ms == 30000);
  CHECK(zoneIs(c.target.zones[6], 136, 79, 0, 255, 255));

  // a lamp that wakes up long after a change does not replay the candle
  Director late;
  CHECK(late.update(&plan, inputs(1795707900UL + FRESH_ENTRY_S + 1, 600), c));
  CHECK(std::strcmp(c.target.name, "warmwit") == 0 && c.transition == Transition::Fade && c.duration_ms == QUICK_FADE_MS);
  // but one that wakes up inside the window does
  Director fresh;
  CHECK(fresh.update(&plan, inputs(1795707900UL + FRESH_ENTRY_S, 600), c));
  CHECK(c.transition == Transition::CandleOn);

  // a new fetch with the same light does not move the lamp; reset() makes it show again
  Plan again = plan;
  CHECK(!late.update(&again, inputs(1795707900UL + 200, 600), c));
  late.reset();
  CHECK(late.update(&again, inputs(1795707900UL + 201, 600), c));
  CHECK(c.duration_ms == QUICK_FADE_MS);

  // the plan runs out: the fallback window takes over (17:00-23:00)
  Director out;
  CHECK(out.update(&plan, inputs(plan.until - 1, 18 * 60), c) && c.source == Source::Plan);
  CHECK(out.update(&plan, inputs(plan.until, 18 * 60), c));
  CHECK(c.source == Source::Fallback && zoneIs(c.target.zones[0], 0, 0, 0, 255, 200));
  CHECK(c.transition == Transition::Fade && c.duration_ms == QUICK_FADE_MS);
  CHECK(!out.update(&plan, inputs(plan.until + 60, 18 * 60 + 1), c));
  CHECK(out.update(&plan, inputs(plan.until + 5 * 3600, 23 * 60), c));
  CHECK(c.source == Source::Fallback && zoneIs(c.target.zones[3], 0, 0, 0, 0, 0));
  CHECK(std::strcmp(out.sceneName(), "fallback off") == 0);

  // never a plan
  Director none;
  CHECK(none.update(nullptr, inputs(T0, 12 * 60), c));
  CHECK(c.source == Source::Fallback && zoneIs(c.target.zones[0], 0, 0, 0, 0, 0));
  CHECK(none.update(nullptr, inputs(T0 + 5 * 3600, 17 * 60), c));
  CHECK(zoneIs(c.target.zones[0], 0, 0, 0, 255, 200));
  Plan invalid = {};
  Director with_invalid;
  CHECK(with_invalid.update(&invalid, inputs(T0, 18 * 60), c) && c.source == Source::Fallback);

  // no clock at all: on, also with a plan that cannot be placed in time
  Director blind;
  CHECK(blind.update(&plan, inputs(0, -1), c));
  CHECK(c.source == Source::Fallback && zoneIs(c.target.zones[6], 0, 0, 0, 255, 200));
  // the clock arrives: the plan takes over
  CHECK(blind.update(&plan, inputs(1795676940UL + 300, 600), c));
  CHECK(c.source == Source::Plan && std::strcmp(c.target.name, "dag") == 0);
}

// candle() from web/src/lights.js, for the lamp's seven zones (index 0 = foot)
static double referenceCandle(int zone, double t, double duration, bool on) {
  int order[ZONE_COUNT];
  for (int k = 0; k < ZONE_COUNT; k++) order[k] = on ? ZONE_COUNT - 1 - k : k;   // top down, or reversed
  const double step = duration / ZONE_COUNT;
  for (int k = 0; k < ZONE_COUNT; k++) {
    if (order[k] != zone) continue;
    const double x = std::fmin(1.0, std::fmax(0.0, (t - k * step) / step));
    return on ? x : 1 - x;
  }
  return -1;
}

static void testSchedule() {
  ZoneStep steps[ZONE_COUNT];
  schedule(Transition::Fade, 5000, steps);
  for (int z = 0; z < ZONE_COUNT; z++) CHECK(steps[z].delay_ms == 0 && steps[z].duration_ms == 5000);
  CHECK(std::fabs(progress(steps[0], 2500) - 0.5f) < 1e-6);

  schedule(Transition::CandleOn, 30000, steps);
  CHECK(steps[6].delay_ms == 0);                                   // the roof first
  CHECK(steps[0].delay_ms == 6 * (30000 / 7));                     // the foot last
  for (int z = 0; z + 1 < ZONE_COUNT; z++) CHECK(steps[z].delay_ms > steps[z + 1].delay_ms);
  schedule(Transition::CandleOff, 30000, steps);
  CHECK(steps[0].delay_ms == 0);                                   // the foot first
  for (int z = 0; z + 1 < ZONE_COUNT; z++) CHECK(steps[z].delay_ms < steps[z + 1].delay_ms);
  for (int z = 0; z < ZONE_COUNT; z++) CHECK(steps[z].duration_ms == 30000 / 7);
  CHECK(steps[6].delay_ms + steps[6].duration_ms <= 30000);

  // the same course as the web app's candle(), every 100 ms
  double worst = 0;
  for (int on = 0; on < 2; on++) {
    schedule(on ? Transition::CandleOn : Transition::CandleOff, 30000, steps);
    for (uint32_t t = 0; t <= 31000; t += 100) {
      for (int z = 0; z < ZONE_COUNT; z++) {
        const double p = progress(steps[z], t);
        const double mine = on ? p : 1 - p;                        // going out: from 1 down to 0
        worst = std::fmax(worst, std::fabs(mine - referenceCandle(z, t / 1000.0, 30.0, on != 0)));
      }
    }
  }
  CHECK(worst < 0.002);                                            // whole milliseconds against 30/7 s
  std::printf("  candle against the reference: largest difference %.5f\n", worst);

  schedule(Transition::CandleOn, 0, steps);
  for (int z = 0; z < ZONE_COUNT; z++) CHECK(progress(steps[z], 0) == 1.0f);
}

static void testPolling() {
  CHECK(pollDelay(60, 0) == 60);
  CHECK(pollDelay(60, 1) == 120);
  CHECK(pollDelay(60, 3) == 480);
  CHECK(pollDelay(60, 4) == 900);
  CHECK(pollDelay(60, 200) == 900);
  CHECK(pollDelay(0, 0) == 15);
  CHECK(pollDelay(3600, 5) == 3600);

  Url u;
  CHECK(parseUrl("https://utrechtdom.city/api/licht", u));
  CHECK(u.tls && u.port == 443 && !std::strcmp(u.host, "utrechtdom.city") && !std::strcmp(u.path, "/api/licht"));
  CHECK(parseUrl("http://192.168.1.20:8080/licht.json?x=1", u));
  CHECK(!u.tls && u.port == 8080 && !std::strcmp(u.host, "192.168.1.20") && !std::strcmp(u.path, "/licht.json?x=1"));
  CHECK(parseUrl("https://example.org", u) && !std::strcmp(u.path, "/") && u.port == 443);
  CHECK(!parseUrl("utrechtdom.city/api/licht", u));
  CHECK(!parseUrl("ftp://example.org/", u));
  CHECK(!parseUrl("https:///api", u));
  CHECK(!parseUrl("https://example.org:0/", u));
  CHECK(!parseUrl("https://example.org:99999/", u));
  CHECK(!parseUrl("https://user@example.org/", u));
  CHECK(!parseUrl("https://example.org/a b", u));
  CHECK(!parseUrl("", u) && !parseUrl(nullptr, u));
  CHECK(!parseUrl(("https://example.org/" + std::string(200, 'a')).c_str(), u));
}

static HttpState http(std::string raw, bool eof, HttpResponse &r, std::vector<char> &buf) {
  buf.assign(raw.begin(), raw.end());
  buf.push_back('#');                       // the spare byte the function may overwrite
  return parseHttpResponse(buf.data(), raw.size(), eof, r);
}

static void testHttp(const std::string &dir) {
  HttpResponse r;
  std::vector<char> buf;

  // what the server really sent (captured through its TLS, chunked)
  const std::string raw = readFile(dir + "/response-chunked.raw");
  CHECK(raw.size() > 1000);
  CHECK(http(raw, false, r, buf) == HttpState::Complete);
  CHECK(r.status == 200);
  Plan plan;
  CHECK(parsePlan(r.body, r.body_len, plan) == ParseError::Ok);
  CHECK(plan.valid && plan.count >= 1 && r.body[r.body_len] == 0);
  std::printf("  captured response: %zu bytes, body %zu bytes, %d plan entries\n", raw.size(), r.body_len, plan.count);
  // arriving in pieces: incomplete until the last byte is in
  bool early = false;
  for (size_t n = 0; n + 1 < raw.size(); n += 7)
    if (http(raw.substr(0, n), false, r, buf) != HttpState::Incomplete) early = true;
  CHECK(!early);
  CHECK(http(raw.substr(0, raw.size() - 9), true, r, buf) == HttpState::Bad);   // closed halfway

  CHECK(http("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello", false, r, buf) == HttpState::Complete);
  CHECK(r.status == 200 && r.body_len == 5 && !std::strcmp(r.body, "hello"));
  CHECK(http("HTTP/1.1 200 OK\r\ncontent-length: 5\r\n\r\nhel", false, r, buf) == HttpState::Incomplete);
  CHECK(http("HTTP/1.1 200 OK\r\ncontent-length: 5\r\n\r\nhel", true, r, buf) == HttpState::Bad);
  CHECK(http("HTTP/1.1 200 OK\r\nServer: x\r\n\r\nhello", false, r, buf) == HttpState::Incomplete);
  CHECK(http("HTTP/1.1 200 OK\r\nServer: x\r\n\r\nhello", true, r, buf) == HttpState::Complete);
  CHECK(r.body_len == 5 && !std::strcmp(r.body, "hello"));
  CHECK(http("HTTP/1.1 308 Permanent Redirect\r\nLocation: https://x/\r\nContent-Length: 0\r\n\r\n", false, r, buf) ==
        HttpState::Complete);
  CHECK(r.status == 308 && r.body_len == 0);
  CHECK(http("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n10\r\n0123456789abcdef\r\n0\r\n\r\n",
             false, r, buf) == HttpState::Complete);
  CHECK(r.body_len == 19 && !std::strcmp(r.body, "abc0123456789abcdef"));
  CHECK(http("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\nabc\r\n0\r\n\r\n", false, r, buf) ==
        HttpState::Bad);
  CHECK(http("SSH-2.0-OpenSSH\r\n\r\n", false, r, buf) == HttpState::Bad);
  CHECK(http("HTTP/1.1 200", false, r, buf) == HttpState::Incomplete);
  CHECK(http("", true, r, buf) == HttpState::Bad);
}

static void testPersistence(const std::string &dir) {
  Plan plan, back;
  CHECK(parse(readFile(dir + "/licht-sample.json"), plan) == ParseError::Ok);
  uint8_t buf[PACKED_PLAN_MAX];
  const size_t n = packPlan(plan, buf, sizeof(buf));
  CHECK(n > 0 && n <= PACKED_PLAN_MAX);
  std::printf("  packed plan: %zu bytes for %d entries\n", n, plan.count);
  CHECK(unpackPlan(buf, n, back));
  CHECK(back.valid && back.time == plan.time && back.until == plan.until && back.since == plan.since);
  CHECK(back.next_s == plan.next_s && back.count == plan.count && back.truncated == plan.truncated);
  CHECK(!std::strcmp(back.now.name, plan.now.name) && sameZones(back.now, plan.now));
  for (int i = 0; i < plan.count; i++) {
    CHECK(back.entries[i].at == plan.entries[i].at && back.entries[i].transition == plan.entries[i].transition);
    CHECK(back.entries[i].duration_s == plan.entries[i].duration_s);
    CHECK(!std::strcmp(back.entries[i].scene.name, plan.entries[i].scene.name));
    CHECK(sameZones(back.entries[i].scene, plan.entries[i].scene));
  }
  // damage anywhere is noticed
  bool accepted = false;
  for (size_t i = 0; i < n; i++) {
    buf[i] ^= 0x10;
    if (unpackPlan(buf, n, back) || back.valid) accepted = true;
    buf[i] ^= 0x10;
  }
  CHECK(!accepted);
  CHECK(!unpackPlan(buf, n - 1, back));
  CHECK(!unpackPlan(buf, 0, back));
  CHECK(packPlan(plan, buf, 20) == 0);
  Plan invalid = {};
  CHECK(packPlan(invalid, buf, sizeof(buf)) == 0);

  // a full plan fits the buffer
  std::string many;
  for (int i = 0; i < MAX_ENTRIES; i++) {
    if (i) many += ",";
    many += entryJson(T0 + 600 * (uint32_t)(i + 1), "s", "kaars_aan", 30, i);
  }
  CHECK(parse(docJson(many), plan) == ParseError::Ok && plan.count == MAX_ENTRIES);
  const size_t full = packPlan(plan, buf, sizeof(buf));
  CHECK(full > 0 && full <= PACKED_PLAN_MAX);
  std::printf("  packed plan, every entry used: %zu bytes (buffer %zu)\n", full, (size_t)PACKED_PLAN_MAX);
  CHECK(unpackPlan(buf, full, back) && back.count == MAX_ENTRIES && back.entries[MAX_ENTRIES - 1].scene.zones[0].level == MAX_ENTRIES - 1);
  CHECK(crc32((const uint8_t *)"123456789", 9) == 0xCBF43926UL);   // the standard check value
}

static void testLink() {
  Zone zones[ZONE_COUNT] = {};
  std::string line = "Z";
  for (int z = 0; z < ZONE_COUNT; z++) {
    char part[11];
    std::snprintf(part, sizeof(part), "%02x%02X%02x%02x%02x", 10 + z, 200, 0, 255, z);
    line += part;
  }
  CHECK(line.size() == LINK_LINE_LEN);
  CHECK(parseLinkLine(line.c_str(), line.size(), zones));
  CHECK(zones[0].r == 10 && zones[0].g == 200 && zones[0].b == 0 && zones[0].w == 255 && zones[0].level == 0);
  CHECK(zones[6].r == 16 && zones[6].level == 6);
  const std::string cr = line + "\r";
  CHECK(parseLinkLine(cr.c_str(), cr.size(), zones));
  CHECK(!parseLinkLine(line.c_str(), line.size() - 1, zones));          // cut off
  const std::string longer = line + "0";
  CHECK(!parseLinkLine(longer.c_str(), longer.size(), zones));
  std::string bad = line;
  bad[5] = 'g';
  CHECK(!parseLinkLine(bad.c_str(), bad.size(), zones));
  bad = line;
  bad[0] = 'X';
  CHECK(!parseLinkLine(bad.c_str(), bad.size(), zones));
  CHECK(!parseLinkLine("", 0, zones));
  CHECK(!parseLinkLine(":", 1, zones));                                   // the relay's sign of life
  CHECK(validLinkCode("ABCDEFGH") && validLinkCode("Z2345678"));
  CHECK(!validLinkCode("ABCDEFG") && !validLinkCode("ABCDEFGHJ") && !validLinkCode(""));
  CHECK(!validLinkCode("ABCDEFG1") && !validLinkCode("ABCDEFGO") && !validLinkCode("abcdefgh") && !validLinkCode(nullptr));
}

int main(int argc, char **argv) {
  const std::string dir = argc > 1 ? argv[1] : ".";
  testParseSample(dir);
  testParseErrors();
  testFallback();
  testDirector(dir);
  testSchedule();
  testPolling();
  testHttp(dir);
  testPersistence(dir);
  testLink();
  std::printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
