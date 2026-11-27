// Domlamp core: everything that can be decided without hardware.
// Plain C++ (no Arduino, no WLED, no heap), so the same file compiles into the usermod and into the
// host test (firmware/test/test_core.cpp).
//
// The lighting plan comes from GET /api/licht (generator: web/src/lightplan.js). Its JSON field names are
// Dutch and stay Dutch here; the C++ names are English.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace domlamp {

constexpr int ZONE_COUNT = 7;                 // foot first: voet .. dak
constexpr int MAX_ENTRIES = 24;               // the sample holds 8 changes for 36 h
constexpr int SCENE_NAME_LEN = 20;            // including the terminating zero
constexpr uint32_t PLAN_HORIZON_S = 36UL * 3600UL;  // the server lists every change in the next 36 h
constexpr uint32_t FRESH_ENTRY_S = 90;        // an entry reached less than this ago gets its own transition
constexpr uint32_t QUICK_FADE_MS = 2000;      // every other change of target (boot, re-enable, fallback)
constexpr uint32_t MIN_VALID_UNIX = 1700000000UL;   // anything earlier is not a set clock

extern const char *const ZONE_NAMES[ZONE_COUNT];

enum class Transition : uint8_t { Fade = 0, CandleOn = 1, CandleOff = 2 };

struct Zone {
  uint8_t r, g, b, w, level;                  // level = the zone's brightness ("strength")
};

struct Scene {
  char name[SCENE_NAME_LEN];
  Zone zones[ZONE_COUNT];
};

struct Entry {
  uint32_t at;                                // "om", Unix seconds
  Transition transition;                      // "overgang"
  uint16_t duration_s;                        // "duur_s"
  Scene scene;
};

struct Plan {
  bool valid;
  bool truncated;                             // more than MAX_ENTRIES changes were sent
  uint32_t time;                              // "tijd": the server's clock when it wrote the plan
  uint32_t until;                             // the plan says nothing about the time from here on
  uint32_t since;                             // "nu.sinds"
  uint16_t next_s;                            // "volgende_s"
  uint8_t count;
  Scene now;                                  // "nu"
  Entry entries[MAX_ENTRIES];                 // "plan", ascending in time
};

// ---- parsing -------------------------------------------------------------------------------------------

enum class ParseError : uint8_t {
  Ok = 0,
  Syntax,           // not JSON, or cut off
  Version,          // "versie" missing or not 1
  NoTime,           // "tijd" missing or not a set clock
  ZoneNames,        // "zones" present but not the seven names this firmware knows, in this order
  NoCurrent,        // "nu.zones" missing
  BadZones,         // a zones array that is not 7 x [r,g,b,w,strength] with values 0..255
  BadEntry,         // a plan entry without "om" or "zones", or entries not ascending in time
};

const char *parseErrorText(ParseError e);

// Parses the JSON body into `out`. On any error `out.valid` is false.
ParseError parsePlan(const char *json, size_t len, Plan &out);

// ---- following the plan --------------------------------------------------------------------------------

// Index of the entry in force at `now`: -1 = the "nu" scene, otherwise entries[index].
int activeIndex(const Plan &plan, uint32_t now);
const Scene &sceneAt(const Plan &plan, int index);
// True when the plan has nothing to say about `now` any more.
bool planExpired(const Plan &plan, uint32_t now);
// The next change after `now`, or nullptr.
const Entry *nextEntry(const Plan &plan, uint32_t now);

bool sameZones(const Scene &a, const Scene &b);

// ---- fallback schedule ---------------------------------------------------------------------------------

// "HH:MM" -> minutes after midnight, or -1.
int parseClockTime(const char *text);
// On between on_min and off_min (local time, minutes after midnight); the window may cross midnight.
// Equal times mean always on.
bool fallbackIsOn(int on_min, int off_min, int minute_of_day);
// Warm white on the W channel only, the same in every zone; level 0 = off.
Scene fallbackScene(bool on, uint8_t level);

// ---- deciding what to show -----------------------------------------------------------------------------

enum class Source : uint8_t { None = 0, Plan, Fallback };

struct Inputs {
  uint32_t now;            // Unix seconds, 0 = the lamp has no clock
  int minute_of_day;       // local time, -1 = unknown
  int fallback_on_min;
  int fallback_off_min;
  uint8_t fallback_level;
};

struct Change {
  Scene target;
  Source source;
  Transition transition;
  uint32_t duration_ms;
};

// Keeps track of the target last handed out and reports when it changes.
class Director {
 public:
  // `plan` may be nullptr or invalid. Returns true and fills `out` when the lamp has to move.
  bool update(const Plan *plan, const Inputs &in, Change &out);
  // Forget the last target: the next update() reports a change even when nothing changed
  // (after "follow" is switched on again, after boot presets, after segments were created).
  void reset() { has_shown_ = false; }
  Source source() const { return source_; }
  const char *sceneName() const { return shown_.name; }

 private:
  Scene shown_ = {};
  bool has_shown_ = false;
  Source source_ = Source::None;
};

// ---- transitions ---------------------------------------------------------------------------------------

struct ZoneStep {
  uint32_t delay_ms;       // after the start of the transition
  uint32_t duration_ms;    // of this zone's own fade
};

// Fade: every zone at once over the whole duration.
// CandleOn: one zone after another from the top down, CandleOff: from the foot up; each zone fades for
// duration / 7 and starts when the one before it is done (as candle() in web/src/lights.js).
void schedule(Transition transition, uint32_t duration_ms, ZoneStep out[ZONE_COUNT]);
// 0..1: how far a zone is at t_ms after the start.
float progress(const ZoneStep &step, uint32_t t_ms);

// ---- polling -------------------------------------------------------------------------------------------

// Seconds until the next poll: the base interval, doubled for every failure in a row, at most 15 minutes.
uint32_t pollDelay(uint32_t base_s, uint8_t failures);

struct Url {
  bool tls;
  uint16_t port;
  char host[64];
  char path[128];
};
// Accepts http:// and https://, an optional port and path.
bool parseUrl(const char *text, Url &out);

enum class HttpState : uint8_t { Incomplete, Complete, Bad };
struct HttpResponse {
  int status;
  char *body;              // inside the buffer handed in, zero-terminated when Complete
  size_t body_len;
};
// Looks at what has been received so far. `eof` = the server closed the connection. When the response is
// complete a chunked body is joined in place. Needs one spare byte after `len` for the terminating zero.
HttpState parseHttpResponse(char *buf, size_t len, bool eof, HttpResponse &out);

// ---- link ---------------------------------------------------------------------------------------------

// One line of the link stream (web/deploy/koppel/koppel.py): "Z" and 70 hex digits, 7 zones x
// [r, g, b, w, strength], foot first. A trailing \r is allowed. Anything else is not a frame.
constexpr size_t LINK_LINE_LEN = 1 + ZONE_COUNT * 5 * 2;
bool parseLinkLine(const char *line, size_t len, Zone out[ZONE_COUNT]);
// A link code: 8 characters from A-Z and 2-9 without I, O, 0 and 1 (as the order numbers of the site).
constexpr int LINK_CODE_LEN = 8;
bool validLinkCode(const char *text);

// ---- persistence ---------------------------------------------------------------------------------------

constexpr size_t PACKED_PLAN_MAX = 16 + 24 + SCENE_NAME_LEN + ZONE_COUNT * 5
                                   + MAX_ENTRIES * (8 + SCENE_NAME_LEN + ZONE_COUNT * 5);
// Fixed byte layout with a magic number and a CRC, independent of struct padding. Returns the size, 0 if
// `cap` is too small.
size_t packPlan(const Plan &plan, uint8_t *buf, size_t cap);
bool unpackPlan(const uint8_t *buf, size_t len, Plan &out);
uint32_t crc32(const uint8_t *data, size_t len);

}  // namespace domlamp
