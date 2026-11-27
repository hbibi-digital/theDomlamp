// Domlamp: a WLED usermod that makes a model of the Dom tower follow the lighting of the real one.
//
// It fetches a small plan (GET /api/licht, see web/src/lightplan.js) and drives seven zones of the lamp,
// each zone one or two WLED segments. It only sets the first colour and the opacity of those segments;
// their pixel ranges, the master brightness, on/off, effects and everything else stay WLED's and the user's.
//
// The decisions live in domlamp_core.cpp (plain C++, tested on the host); this file is the WLED side.
#include "wled.h"

#ifndef ARDUINO_ARCH_ESP32
#error "The domlamp usermod needs an ESP32 (it fetches over TLS in a FreeRTOS task)."
#endif

#include "domlamp_core.h"
#include "domlamp_fetch.h"
#include "domlamp_link.h"

#define LINK_FADE_MS 120               // between two frames of the link (the browser sends at most every 70 ms)
#define LINK_HOLD_MS 12000             // back to the tower this long after the last frame (as lamplink.js HOLD_S)
#ifndef DOMLAMP_VERSION
#define DOMLAMP_VERSION "dev"           // set by the build (platformio_override.ini)
#endif
#ifndef DOMLAMP_URL
#define DOMLAMP_URL "https://utrechtdom.city/api/licht"
#endif

using namespace domlamp;

class DomlampUsermod : public Usermod {
 private:
  // ---- settings (cfg.json, usermod settings page) ----
  bool enabled = true;                 // "follow the Dom tower"; also in the JSON state
  String url = DOMLAMP_URL;
  uint16_t intervalS = 0;              // 0 = as often as the server asks ("volgende_s")
  bool verifyCertificate = true;
  uint8_t firstSegment = 0;            // the segment of the lowest zone
  bool twoSided = true;                // two segments for each zone: strip going up, strip coming down
  bool createSegments = true;          // make evenly divided segments when WLED has only its single default
  String fallbackOn = "17:00";
  String fallbackOff = "23:00";
  uint8_t fallbackLevel = 255;
  uint8_t persistHours = 6;            // keep the plan in flash at most this often; 0 = never
  String linkUrl = "";                 // the relay for linking through the server (domlamp_link.h); empty = off
  String linkCode = "";                // this lamp's code at the relay; made once, at random

  // ---- state ----
  Fetcher fetcher;
  Linker linker;
  bool linkWanted = false;             // a valid relay address and code
  Url linkParsed = {};
  Director director;
  Plan plan = {};
  Url parsedUrl = {};
  bool urlValid = false;
  bool initDone = false;
  bool updating = false;               // a firmware update is running
  bool everTried = false;
  bool segmentsChecked = false;
  bool waitingForClock = false;
  uint8_t failures = 0;
  uint8_t lastSegmentCount = 0;
  unsigned long lastAttemptMs = 0;
  unsigned long lastOkMs = 0;
  unsigned long lastDecisionMs = 0;
  bool everOk = false;
  bool planFromFlash = false;
  uint32_t savedPlanTime = 0;          // "tijd" of the plan in flash
  int32_t clockSkewS = 0;              // lamp clock minus server clock at the last fetch
  uint32_t lastTookMs = 0;
  uint32_t lastStackFree = 0;
  char lastError[72] = "";

  // the transition in progress
  Scene target = {};
  ZoneStep steps[ZONE_COUNT] = {};
  bool zonePending[ZONE_COUNT] = {};
  bool transitionRunning = false;

  // "hold": something else drives the segments for a while (the web app linked to this lamp, lamplink.js).
  // It asks for a number of seconds and keeps asking; when it stops, the lamp returns to the tower by itself.
  bool held = false;
  unsigned long holdStartMs = 0;
  uint32_t holdMs = 0;
  unsigned long transitionStartMs = 0;

  static const char _name[];
  static const char _enabled[];
  static const char _planFile[];

  // ---- clock ----

  // ---- link through the server ----

  void configureLink() {
    linkWanted = false;
    if (!linkUrl.length()) return;
    if (!validLinkCode(linkCode.c_str())) {       // first use: a code of its own, kept in the settings
      static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
      char code[LINK_CODE_LEN + 1];
      for (int k = 0; k < LINK_CODE_LEN; k++) code[k] = alphabet[esp_random() % 32];
      code[LINK_CODE_LEN] = 0;
      linkCode = code;
      configNeedsWrite = true;
    }
    String full = linkUrl;
    while (full.endsWith("/")) full.remove(full.length() - 1);
    full += F("/lamp/");
    full += linkCode;
    linkWanted = parseUrl(full.c_str(), linkParsed);
  }

  // Keeps the stream to the relay up and shows what arrives on it. A frame holds the segments as the direct
  // link does: the lamp returns to the tower by itself when the frames stop.
  void runLink() {
    linker.setClock(clockOk() ? toki.second() : 0);
    if (!linkWanted || updating) {
      if (linker.running()) linker.stop();
      return;
    }
    if (!linker.running() && WLED_CONNECTED && millis() > 10000) linker.start(linkParsed, verifyCertificate);
    LinkFrame frame;
    if (!linker.take(frame)) return;
    if (frame.end) {
      if (held) holdMs = 0;
      return;
    }
    bool changed = false;
    for (int z = 0; z < ZONE_COUNT; z++) changed |= applyZone(z, frame.zones[z], LINK_FADE_MS);
    transitionRunning = false;
    held = true;
    holdStartMs = millis();
    holdMs = LINK_HOLD_MS;
    if (changed) {
      strip.trigger();
      interfaceUpdateCallMode = CALL_MODE_WS_SEND;
    }
  }

  static bool clockOk() { return toki.getTimeSource() != TOKI_TS_NONE && toki.second() >= MIN_VALID_UNIX; }

  // ---- segments ----

  uint8_t segmentsPerZone() const { return twoSided ? 2 : 1; }

  // The segment of a zone: side 0 counts up from firstSegment; side 1 mirrors it, because the strip comes
  // back down the other face of the mast (zone 6 = segment 7, zone 0 = segment 13).
  int segmentOf(int zone, int side) const {
    return side == 0 ? firstSegment + zone : firstSegment + (2 * ZONE_COUNT - 1 - zone);
  }

  bool applyZone(int zone, const Zone &z, uint32_t durationMs) {
    const uint32_t color = RGBW32(z.r, z.g, z.b, z.w);
    bool changed = false;
    for (int side = 0; side < segmentsPerZone(); side++) {
      const int id = segmentOf(zone, side);
      if (id >= strip.getSegmentsNum()) continue;
      Segment &seg = strip.getSegment(id);
      if (!seg.isActive()) continue;
      if (seg.colors[0] == color && seg.opacity == z.level && seg.on) continue;
      // WLED's own segment transition does the fade; it has to start before the values change.
      // Its timer is 16 bits of milliseconds.
      seg.startTransition((uint16_t)min(durationMs, (uint32_t)65000), blendingStyle != TRANSITION_FADE);
      seg.colors[0] = color;
      seg.opacity = z.level;
      seg.on = true;
      changed = true;
    }
    return changed;
  }

  void startTransition(const Change &change) {
    target = change.target;
    schedule(change.transition, change.duration_ms, steps);
    for (int z = 0; z < ZONE_COUNT; z++) zonePending[z] = true;
    transitionRunning = true;
    transitionStartMs = millis();
  }

  void runTransition() {
    if (!transitionRunning) return;
    const uint32_t elapsed = millis() - transitionStartMs;
    bool anyPending = false, changed = false;
    for (int z = 0; z < ZONE_COUNT; z++) {
      if (!zonePending[z]) continue;
      if (elapsed >= steps[z].delay_ms) {
        changed |= applyZone(z, target.zones[z], steps[z].duration_ms);
        zonePending[z] = false;
      } else {
        anyPending = true;
      }
    }
    transitionRunning = anyPending;
    if (changed) {
      strip.trigger();
      // Tell the web UI. Not stateUpdated(): that restarts every segment's transition with WLED's
      // default time, which would cut a candle short.
      interfaceUpdateCallMode = CALL_MODE_WS_SEND;
    }
  }

  // Evenly divided segments as a start, only on a WLED that still has its single default segment.
  void createDefaultSegments() {
    const unsigned needed = ZONE_COUNT * segmentsPerZone();
    if (!createSegments || strip.getSegmentsNum() != 1 || firstSegment > 1) return;
    const unsigned total = strip.getLengthTotal();
    if (total < needed || firstSegment + needed > WS2812FX::getMaxSegments()) return;
    strip.suspend();
    strip.waitForIt();
    for (unsigned j = 0; j < needed; j++) {
      const unsigned start = total * j / needed, stop = total * (j + 1) / needed;
      const unsigned id = firstSegment + j;
      if (id < strip.getSegmentsNum()) strip.getSegment(id).setGeometry(start, stop);
      else strip.appendSegment(start, stop);
      if (id >= strip.getSegmentsNum()) break;
      const int zone = j < ZONE_COUNT ? (int)j : (int)(2 * ZONE_COUNT - 1 - j);
      char label[32];
      snprintf_P(label, sizeof(label), twoSided ? (j < ZONE_COUNT ? PSTR("%s op") : PSTR("%s neer")) : PSTR("%s"),
                 ZONE_NAMES[zone]);
      Segment &seg = strip.getSegment(id);
      seg.setName(label);
      // A segment made with appendSegment() does not know yet what its LEDs can do. Without this WLED took
      // it for an on/off segment and forced its colour to full white on all four channels (seen on the
      // first lamp, 25 Nov 2026: info "seglc" 3 for segment 0 and 0 for the other thirteen).
      seg.refreshLightCapabilities();
      seg.colors[0] = BLACK;       // dark until the first scene fades in
      seg.opacity = 0;
    }
    strip.resume();
    stateChanged = true;
    DEBUG_PRINTF_P(PSTR("Domlamp: created %u default segments\n"), needed);
  }

  // ---- the plan in flash ----

  void loadPlan() {
    if (!WLED_FS.exists(FPSTR(_planFile))) return;
    File f = WLED_FS.open(FPSTR(_planFile), "r");
    if (!f) return;
    const size_t size = f.size();
    uint8_t *buf = size > 0 && size <= PACKED_PLAN_MAX ? static_cast<uint8_t *>(malloc(size)) : nullptr;
    if (buf && f.read(buf, size) == size && unpackPlan(buf, size, plan)) {
      planFromFlash = true;
      savedPlanTime = plan.time;
    }
    free(buf);
    f.close();
  }

  // Flash wear: at most one write of under 2 kB every persistHours, measured on the plan's own clock, so
  // a lamp that keeps restarting does not write more often either.
  void maybeSavePlan() {
    if (persistHours == 0 || !plan.valid) return;
    if (savedPlanTime != 0 && plan.time >= savedPlanTime && plan.time - savedPlanTime < persistHours * 3600UL) return;
    uint8_t *buf = static_cast<uint8_t *>(malloc(PACKED_PLAN_MAX));
    if (!buf) return;
    const size_t size = packPlan(plan, buf, PACKED_PLAN_MAX);
    if (size) {
      File f = WLED_FS.open(FPSTR(_planFile), "w");
      if (f) {
        if (f.write(buf, size) == size) savedPlanTime = plan.time;
        f.close();
      }
    }
    free(buf);
  }

  // ---- fetching ----

  void handleFetchResult() {
    const FetchResult *result = fetcher.take();
    if (!result) return;
    lastTookMs = result->took_ms;
    lastStackFree = result->stack_free;
    if (!result->ok) {
      if (failures < 250) failures++;
      strlcpy(lastError, result->error, sizeof(lastError));
      DEBUG_PRINTF_P(PSTR("Domlamp: fetch failed: %s\n"), lastError);
      return;
    }
    plan = result->plan;
    planFromFlash = false;
    failures = 0;
    lastError[0] = 0;
    everOk = true;
    lastOkMs = millis();
    if (clockOk()) {
      clockSkewS = (int32_t)(toki.second() - plan.time);
    } else {
      // No clock of its own (NTP off or unreachable): the server's will do. A low-ranking source, so NTP
      // and the browser still overrule it. The plan is written every half minute, so this runs a little late.
      toki.setTime(plan.time, TOKI_NO_MS_ACCURACY, TOKI_TS_SEC);
      updateLocalTime();
      clockSkewS = 0;
    }
    maybeSavePlan();
  }

  void maybeFetch() {
    if (fetcher.busy() || !urlValid || !WLED_CONNECTED || millis() < 8000) return;
    const uint32_t base = intervalS ? intervalS : (plan.valid && !planFromFlash ? plan.next_s : 60);
    if (everTried && millis() - lastAttemptMs < pollDelay(base, failures) * 1000UL) return;
    // A certificate can only be checked against a known date.
    waitingForClock = parsedUrl.tls && verifyCertificate && !clockOk();
    if (waitingForClock) return;
    FetchRequest request;
    request.url = parsedUrl;
    request.verify = verifyCertificate;
    request.now = toki.second();
    everTried = true;
    lastAttemptMs = millis();
    if (!fetcher.start(request)) {
      const FetchResult *result = fetcher.take();   // the task could not be made
      if (result) strlcpy(lastError, result->error, sizeof(lastError));
      if (failures < 250) failures++;
    }
  }

  void decide() {
    if (millis() < 3000) return;                    // let WLED apply its boot preset first
    if (!segmentsChecked) {
      segmentsChecked = true;
      createDefaultSegments();
      director.reset();
    }
    if (strip.getSegmentsNum() != lastSegmentCount) {   // segments added or removed: show the scene on them
      lastSegmentCount = strip.getSegmentsNum();
      director.reset();
    }
    Inputs in;
    in.now = clockOk() ? toki.second() : 0;
    in.minute_of_day = clockOk() ? hour(localTime) * 60 + minute(localTime) : -1;
    in.fallback_on_min = parseClockTime(fallbackOn.c_str());
    in.fallback_off_min = parseClockTime(fallbackOff.c_str());
    if (in.fallback_on_min < 0) in.fallback_on_min = 17 * 60;
    if (in.fallback_off_min < 0) in.fallback_off_min = 23 * 60;
    in.fallback_level = fallbackLevel;
    Change change;
    if (director.update(&plan, in, change)) startTransition(change);
  }

  static void addInfoRow(JsonObject &user, const __FlashStringHelper *label, const String &text) {
    JsonArray row = user.createNestedArray(label);
    row.add(text);
  }

  static String ago(unsigned long sinceMs) {
    const unsigned long s = (millis() - sinceMs) / 1000;
    if (s < 120) return String(s) + F(" s ago");
    if (s < 7200) return String(s / 60) + F(" min ago");
    return String(s / 3600) + F(" h ago");
  }

 public:
  void setup() override {
    urlValid = parseUrl(url.c_str(), parsedUrl);
    configureLink();
    loadPlan();
    initDone = true;
  }

  void loop() override {
    if (!initDone) return;
    if (!enabled || updating) {
      if (linker.running()) linker.stop();
      return;
    }
    handleFetchResult();
    maybeFetch();
    runLink();
    if (held) {
      if (millis() - holdStartMs < holdMs) return;  // the plan is still fetched, the segments are left alone
      held = false;
      transitionRunning = false;
      director.reset();                             // show the tower's scene again
    }
    if (millis() - lastDecisionMs >= 250) {
      lastDecisionMs = millis();
      decide();
    }
    runTransition();
  }

  // Free the memory and the network for a firmware update.
  void onUpdateBegin(bool init) override { updating = init; }

  // A boot preset may arrive after the first scene was shown; in the first seconds any change of state
  // makes the scene show again. After that a change by hand is left alone until the next change of scene.
  void onStateChange(uint8_t mode) override {
    (void)mode;
    if (millis() < 15000) director.reset();
  }

  void addToJsonInfo(JsonObject &root) override {
    JsonObject user = root["u"];
    if (user.isNull()) user = root.createNestedObject("u");

    JsonArray head = user.createNestedArray(F("Dom tower"));
    String button = F("<button class=\"btn btn-xs\" onclick=\"requestJson({");
    button += FPSTR(_name);
    button += F(":{");
    button += FPSTR(_enabled);
    button += enabled ? F(":false}});\">") : F(":true}});\">");
    button += F("<i class=\"icons");
    button += enabled ? F(" on") : F(" off");
    button += F("\">&#xe08f;</i></button>");
    head.add(button);
    head.add(!enabled ? F(" not following") : held ? F(" held by a linked app") : F(" following"));
    if (!enabled) return;

    String scene = director.sceneName();
    if (director.source() == Source::Plan) scene += planFromFlash ? F(" (stored plan)") : F(" (plan)");
    else if (!clockOk()) scene += F(" (no clock)");
    addInfoRow(user, F("Dom scene"), scene);

    if (clockOk() && director.source() == Source::Plan) {
      const Entry *next = nextEntry(plan, toki.second());
      if (next) {
        const uint32_t s = next->at - toki.second();
        char text[48];
        snprintf_P(text, sizeof(text), PSTR("%s in %u h %02u min"), next->scene.name, (unsigned)(s / 3600),
                   (unsigned)(s % 3600 / 60));
        addInfoRow(user, F("Dom next"), text);
      }
    }

    String fetch;
    if (everOk) {
      fetch = ago(lastOkMs);
      fetch += F(", ");
      fetch += lastTookMs;
      fetch += F(" ms");
    } else {
      fetch = F("never");
    }
    addInfoRow(user, F("Dom last fetch"), fetch);

    String error;
    if (!urlValid) error = F("the URL in the settings is not valid");
    else if (lastError[0]) error = lastError;
    else if (waitingForClock) error = F("waiting for the clock (switch on NTP)");
    else if (!WLED_CONNECTED) error = F("no network");
    if (error.length()) {
      if (failures > 1) {
        error += F(" (");
        error += failures;
        error += F("x)");
      }
      addInfoRow(user, F("Dom error"), error);
    }
    if (everOk && (clockSkewS > 120 || clockSkewS < -120)) {
      String skew = String(clockSkewS);
      skew += F(" s from the server");
      addInfoRow(user, F("Dom clock"), skew);
    }
    if (linkUrl.length()) {
      String link = linkCode;
      if (!linkWanted) link += F(", the relay address is not valid");
      else if (linker.connected()) link += F(", online");
      else if (linker.error()[0]) { link += F(", "); link += linker.error(); }
      else link += F(", connecting");
      link += F(" (");
      link += linker.frames();
      link += F(" frames, ");
      link += linker.connects();
      link += F(" connects)");
      addInfoRow(user, F("Dom link code"), link);
    }
    addInfoRow(user, F("Dom firmware"), F(DOMLAMP_VERSION));
    if (everTried && lastStackFree) {
      String stack = String(lastStackFree);
      stack += F(" B unused");
      addInfoRow(user, F("Dom fetch stack"), stack);
    }
  }

  // {"Domlamp":{"enabled":false}} in the JSON API or in a preset switches following off, true on again.
  void addToJsonState(JsonObject &root) override {
    if (!initDone) return;
    JsonObject um = root[FPSTR(_name)];
    if (um.isNull()) um = root.createNestedObject(FPSTR(_name));
    um[FPSTR(_enabled)] = enabled;
  }

  void readFromJsonState(JsonObject &root) override {
    if (!initDone) return;
    JsonObject um = root[FPSTR(_name)];
    if (um.isNull()) return;
    const bool was = enabled;
    getJsonValue(um[FPSTR(_enabled)], enabled);
    if (!um[F("hold")].isNull()) {                  // seconds, at most ten minutes; 0 ends the hold at once
      const int seconds = um[F("hold")] | 0;
      held = true;
      holdStartMs = millis();
      holdMs = (uint32_t)constrain(seconds, 0, 600) * 1000UL;
    }
    if (enabled && !was) {
      director.reset();              // show the scene again, whatever was set by hand meanwhile
      everTried = false;             // and fetch a fresh plan
    }
  }

  void addToConfig(JsonObject &root) override {
    JsonObject top = root.createNestedObject(FPSTR(_name));
    top[FPSTR(_enabled)] = enabled;
    top[F("url")] = url;
    top[F("interval")] = intervalS;
    top[F("verify-certificate")] = verifyCertificate;
    top[F("first-segment")] = firstSegment;
    top[F("two-segments-per-zone")] = twoSided;
    top[F("create-segments")] = createSegments;
    top[F("fallback-on")] = fallbackOn;
    top[F("fallback-off")] = fallbackOff;
    top[F("fallback-level")] = fallbackLevel;
    top[F("store-plan-hours")] = persistHours;
    top[F("link-url")] = linkUrl;
    top[F("link-code")] = linkCode;
  }

  bool readFromConfig(JsonObject &root) override {
    JsonObject top = root[FPSTR(_name)];
    bool complete = !top.isNull();
    const bool was = enabled;
    complete &= getJsonValue(top[FPSTR(_enabled)], enabled, true);
    complete &= getJsonValue(top[F("url")], url, DOMLAMP_URL);
    complete &= getJsonValue(top[F("interval")], intervalS, 0);
    complete &= getJsonValue(top[F("verify-certificate")], verifyCertificate, true);
    complete &= getJsonValue(top[F("first-segment")], firstSegment, 0);
    complete &= getJsonValue(top[F("two-segments-per-zone")], twoSided, true);
    complete &= getJsonValue(top[F("create-segments")], createSegments, true);
    complete &= getJsonValue(top[F("fallback-on")], fallbackOn, "17:00");
    complete &= getJsonValue(top[F("fallback-off")], fallbackOff, "23:00");
    complete &= getJsonValue(top[F("fallback-level")], fallbackLevel, 255);
    complete &= getJsonValue(top[F("store-plan-hours")], persistHours, 6);
    const String linkWas = linkUrl + linkCode;
    complete &= getJsonValue(top[F("link-url")], linkUrl, "");
    // no default: a change that does not name the code (the JSON API with only the address) must keep it
    complete &= getJsonValue(top[F("link-code")], linkCode);
    linkCode.toUpperCase();
    if (firstSegment + ZONE_COUNT * 2 > WS2812FX::getMaxSegments()) firstSegment = 0;
    urlValid = parseUrl(url.c_str(), parsedUrl);
    if (initDone) {                  // settings saved while running: apply them now
      director.reset();
      everTried = false;
      failures = 0;
      lastError[0] = 0;
      if (enabled && !was) segmentsChecked = false;
      if (linkWas != linkUrl + linkCode) {         // another relay or code: the stream starts again by itself
        if (linker.running()) linker.stop();
        configureLink();
      }
    }
    return complete;
  }

  void appendConfigData(Print &script) override {
    script.print(F("addInfo('Domlamp:url',1,'http(s)://host/path; https on port 443 only');"));
    script.print(F("addInfo('Domlamp:interval',1,'s between fetches (0 = as the server asks)');"));
    script.print(F("addInfo('Domlamp:verify-certificate',1,'needs the clock (NTP)');"));
    script.print(F("addInfo('Domlamp:first-segment',1,'segment of the lowest zone');"));
    script.print(F("addInfo('Domlamp:two-segments-per-zone',1,'strip up and down: zone 1-7 = segments 0-6 and 13-7');"));
    script.print(F("addInfo('Domlamp:create-segments',1,'only when WLED has one segment');"));
    script.print(F("addInfo('Domlamp:fallback-on',1,'HH:MM, warm white without a plan');"));
    script.print(F("addInfo('Domlamp:fallback-off',1,'HH:MM');"));
    script.print(F("addInfo('Domlamp:fallback-level',1,'0-255');"));
    script.print(F("addInfo('Domlamp:store-plan-hours',1,'h between writes to flash (0 = never)');"));
    script.print(F("addInfo('Domlamp:link-url',1,'relay for linking through the server, e.g. https://utrechtdom.city/api/koppel (empty = off)');"));
    script.print(F("addInfo('Domlamp:link-code',1,'8 characters; empty = make one');"));
  }
};

const char DomlampUsermod::_name[] PROGMEM = "Domlamp";
const char DomlampUsermod::_enabled[] PROGMEM = "enabled";
const char DomlampUsermod::_planFile[] PROGMEM = "/domlamp-plan.bin";

static DomlampUsermod usermod_domlamp;
REGISTER_USERMOD(usermod_domlamp);
