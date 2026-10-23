#include "iotsaNeoClockMod.h"
#include "iotsaConfigFile.h"
#include "iotsaRunmode.h"

#define IFDEBUGX if(0)

extern "C" {
  int _gettimeofday_r(struct _reent* unused, struct timeval *tp, void *tzp);
}

//
// Helper function - combine r/g/b values into rgb with clamping and checking
//
uint32_t IotsaNeoClockMod::combineRGB(uint32_t oldRGB, float factor, int red, int green, int blue) {
  red = (int)(red*factor) + int(red > 0 && factor > 0);
  green = (int)(green*factor) + int(green > 0 && factor > 0);
  blue = (int)(blue*factor) + int(blue > 0 && factor > 0);
  red += (oldRGB >> 16) & 0xff;
  green += (oldRGB >> 8) & 0xff;
  blue += oldRGB & 0xff;
  if (red > 255) red = 255;
  if (green > 255) green = 255;
  if (blue > 255) blue = 255;
  if (red < 0) red = 0;
  if (green < 0) green = 0;
  if (blue < 0) blue = 0;
  return (red << 16) | (green << 8) | blue;
}

//
// Helper function - blend color into a buffer pixel
//
void IotsaNeoClockMod::blendPixel(uint32_t colors[NUM_LEDS], int idx, float factor, uint32_t rgb, float brightness) {
  int red = (rgb >> 16) & 0xff;
  int green = (rgb >> 8) & 0xff;
  int blue = rgb & 0xff;
  factor = factor * brightness;
  colors[idx] = combineRGB(colors[idx], factor, red, green, blue);
}

// Saturating per-channel add of two already-scaled colors (no factor, so no
// combineRGB() round-up floor).
static uint32_t addRGB(uint32_t a, uint32_t b) {
  uint32_t rv = 0;
  for (int shift=0; shift<24; shift+=8) {
    uint32_t c = ((a >> shift) & 0xff) + ((b >> shift) & 0xff);
    rv |= (c > 255 ? 255 : c) << shift;
  }
  return rv;
}

//
// Data provider: clock hands (seconds/minutes/hours).
//
void IotsaNeoClockMod::updateClockFace(uint32_t colors[NUM_LEDS], const ClockTime &time, int ledRotationOffset, float brightness) {
  // Seconds
  {
    // Transition window (width W on each side of a second boundary): a full
    // 0..1 handoff between the outgoing and incoming LED, evaluated so that
    // both sides of a boundary agree exactly at the boundary itself (0.5/0.5)
    // -- this must stay continuous across the frac=1->0 second rollover, or
    // the "self" LED's brightness visibly jumps instead of flowing. W=0.5
    // spans the whole second, so there's no held-still plateau at 1.0 --
    // a narrower W reads as "static, then a quick flick" since most of the
    // second would then pass with nothing changing.
    const float W = 0.5;
    float frac = time.frac;
    float valBefore = 0, valAfter = 0, valSelf;
    if (frac < W) {
      float s = (frac + W) / (2*W);
      valSelf = s;
      valBefore = 1 - s;
    } else if (frac > 1-W) {
      float s = (frac - (1-W)) / (2*W);
      valAfter = s;
      valSelf = 1 - s;
    } else {
      valSelf = 1.0;
    }
    IFDEBUGX { IotsaSerial.printf("seconds=%d, before=%f, self=%f, after=%f", time.seconds, valBefore, valSelf, valAfter); IotsaSerial.println(); }

    int ledSelf = (FIRST_SEC_LED+time.seconds - ledRotationOffset*STRIDE_HOUR_LEDS + NUM_LEDS) % NUM_LEDS;
    int ledBefore = (ledSelf + NUM_LEDS-1) % NUM_LEDS;
    int ledAfter = (ledSelf + 1) % NUM_LEDS;
    if (valBefore) {
      blendPixel(colors, ledBefore, valBefore, COLOR_SEC, brightness);
    }
    if (valSelf) {
      blendPixel(colors, ledSelf, valSelf, COLOR_SEC, brightness);
    }
    if (valAfter) {
      blendPixel(colors, ledAfter, valAfter, COLOR_SEC, brightness);
    }
  }
  // Minute and hour hands are drawn into their own buffers so the (shorter)
  // hour hand can overlay the minute hand instead of blending with it (#11);
  // the combined hands are then added onto the frame like everything else.
  uint32_t hands[NUM_LEDS] = {0};
  uint32_t hourHand[NUM_LEDS] = {0};
  // Minutes
  {
    int minutes = time.minutes;
    int minMod5 = minutes % 5;
    minutes = minutes / 5;
    IFDEBUGX { IotsaSerial.printf("minutes=%d*5 + %d\n", minMod5, minutes);}
    int firstLed = FIRST_MIN_LED + ((minutes-ledRotationOffset+12)%12)*STRIDE_MIN_LEDS;
    if (firstLed >= NUM_LEDS) firstLed -= NUM_LEDS;

    // Set all the pixels corresponding to the minute hand
    for (int i=firstLed; i<firstLed+NUM_MIN_LEDS; i++) {
      blendPixel(hands, i, 1.0, COLOR_MIN, brightness);
    }
    // And set the one corresponding to the current minute again, to highlight it
    blendPixel(hands, firstLed+minMod5, 1.0, COLOR_MIN, brightness);
  }
  // Hours
  {
    int hours = time.hours;
    IFDEBUGX { IotsaSerial.print("hours="); IotsaSerial.println(hours); }
    int firstLed = FIRST_HOUR_LED + ((hours-ledRotationOffset+12)%12)*STRIDE_HOUR_LEDS;
    int extraLed = 0;
    // Determine which segment to show the hour hand (depending on whether where
    // at <30 minutes past the hour or >30 minutes past) and
    // which of the three hour-hand leds to highlight
    float frac = time.minutes/60.0;
    if (frac < 0.1666) {
      extraLed = 0;
    } else
    if (frac < 0.3333) {
      extraLed = 1;
    } else
    if (frac < 0.5) {
      extraLed = 2;
    } else
    if (frac < 0.6666) {
      firstLed += STRIDE_HOUR_LEDS;
      extraLed = 0;
    } else
    if (frac < 0.8333) {
      firstLed += STRIDE_HOUR_LEDS;
      extraLed = 1;
    } else {
      firstLed += STRIDE_HOUR_LEDS;
      extraLed = 2;
    }

    // Light up the correct hour hand
    while (firstLed >= NUM_LEDS) firstLed -= NUM_LEDS;
    for (int i=firstLed; i<firstLed+NUM_HOUR_LEDS; i++) {
      blendPixel(hourHand, i, 1.0, COLOR_HOUR, brightness);
    }
    // And highlight the pixel corresponding to where in the half hour we are
    blendPixel(hourHand, firstLed+extraLed, 1.0, COLOR_HOUR, brightness);
  }
  for (int i=0; i<NUM_LEDS; i++) {
    if (hourHand[i]) hands[i] = hourHand[i];
    colors[i] = addRGB(colors[i], hands[i]);
  }
}

//
// Data provider: main status ring (inner ring, one uniform color)
//
void IotsaNeoClockMod::updateStatus(uint32_t colors[NUM_LEDS], const StatusState &state, float brightness) {
  if (state.color == 0) return;
  for(int i=STRIDE_HOUR_LEDS-1; i<NUM_LEDS; i+=STRIDE_HOUR_LEDS) {
    blendPixel(colors, i, 1.0, state.color, brightness);
  }
}

//
// Data provider: temporal status ring (outer ring, one color per 5-minute segment).
// ledRotationOffset must match updateClockFace's, or this ring ends up rotated
// relative to the clock hands (cwi-dis/iotsaNeoClock#2's underlying 5-year-old bug,
// on the two clocks that need a nonzero offset).
//
void IotsaNeoClockMod::updateTemporalStatus(uint32_t colors[NUM_LEDS], const TemporalStatusState &state, int ledRotationOffset, float brightness) {
  int idx = 0;
  for(int i=0; i<NUM_LEDS; i+=STRIDE_HOUR_LEDS) {
    int segment = (idx + ledRotationOffset) % 12;
    blendPixel(colors, i, 1.0, state.colors[segment], brightness);
    idx++;
  }
}

//
// Alert playback -- overrides the clock/status/temporalStatus entirely while active.
// Not blended, not brightness-scaled: alerts are meant to be shown exactly as given.
//
// Alert pattern files are plain ascii: each line is "duration r g b r g b
// ...", one r/g/b triplet per LED, applied starting at LED 0 for the given
// duration (in milliseconds) before moving to the next line. Playback stops
// at the first line that doesn't start with a positive duration (in
// particular, at end of file).
//
// They live in iotsa's generic /data upload directory alongside arbitrary
// other files, so they're named with an ALERT_FILE_PREFIX prefix to keep
// them distinguishable -- see forEachAlertName() and cwi-dis/iotsaNeoClock#5.
#define ALERT_FILE_PREFIX "alert-"

bool IotsaNeoClockMod::renderAlert() {
  if (!currentAlert) return false;
  // We need to show an alert, or possibly read a new line from the alert file.
  if (millis() > currentAlertLineEndTime) {
    int nextDuration = currentAlert.parseInt();
    if (nextDuration <= 0) {
      stopAlert();
      return false;
    }
    currentAlertLineEndTime = millis() + nextDuration;
    int currentLed = 0;
    while (currentAlert.read() == ' ') {
      int r = currentAlert.parseInt();
      if (currentAlert.read() != ' ') break;
      int g = currentAlert.parseInt();
      if (currentAlert.read() != ' ') break;
      int b = currentAlert.parseInt();
      display.setPixel(currentLed, (r<<16)|(g<<8)|b);
      currentLed++;
    }
  }
  display.show();
  return true;
}

bool IotsaNeoClockMod::startAlert(const String &name) {
  stopAlert();
  String fileName = String("/data/") + ALERT_FILE_PREFIX + name;
  currentAlert = IOTSA_FS.open(fileName, "r");
  if (!currentAlert) return false;
  currentAlertName = name;
  return true;
}

void IotsaNeoClockMod::stopAlert() {
  if (currentAlert) currentAlert.close();
  currentAlertName = "";
}

void IotsaNeoClockMod::forEachAlertName(std::function<void(const String &alertName)> cb) {
  static const int prefixLen = sizeof(ALERT_FILE_PREFIX) - 1;
#ifdef ESP32
  File d = IOTSA_FS.open("/data");
  File f = d.openNextFile();
  while (f) {
    String fileName = f.name();
    if (fileName.startsWith("/data/")) fileName = fileName.substring(6);
    if (fileName.startsWith(ALERT_FILE_PREFIX)) cb(fileName.substring(prefixLen));
    f = d.openNextFile();
  }
#else
  Dir d = IOTSA_FS.openDir("/data");
  while (d.next()) {
    String fileName = d.fileName();
    if (fileName.startsWith("/data/")) fileName = fileName.substring(6);
    if (fileName.startsWith(ALERT_FILE_PREFIX)) cb(fileName.substring(prefixLen));
  }
#endif
}

float IotsaNeoClockMod::brightness() {
  return brightnessMod ? brightnessMod->brightness() : 1.0;
}

void IotsaNeoClockMod::render() {
  if (renderAlert()) return;
  // Only update the clock at most 20 times per second
  if (millis() < lastTimeShown + 50) return;
  lastTimeShown = millis();

  // Clear the status indicators whenever they time out
  if (currentStatusColorEndTime && millis() > currentStatusColorEndTime) {
    currentStatusColorEndTime = 0;
    currentStatusColor = nextStatusColor;
  }
  if (temporalStatusColorEndTime && millis() > temporalStatusColorEndTime) {
    temporalStatusColorEndTime = 0;
    for (int i=0; i<12; i++) temporalStatusColor[i] = 0;
  }

  for (int i=0; i<NUM_LEDS; i++) colors[i] = 0;

  float br = brightness();
  uint32_t statusColor = iotsaStatus.statusColor();
  StatusState statusState = { statusColor ? statusColor : currentStatusColor };
  updateStatus(colors, statusState, br);
  TemporalStatusState temporalState;
  for (int i=0; i<12; i++) temporalState.colors[i] = temporalStatusColor[i];
  updateTemporalStatus(colors, temporalState, ledRotationOffset, br);
  ClockTime time = { ntpMod.localHours12(), ntpMod.localMinutes(), ntpMod.localSeconds(), 0 };
  {
    struct timeval tval = {0, 0};
    _gettimeofday_r(NULL, &tval, NULL);
    time.frac = ((float)tval.tv_usec) / 1000000.0;
  }
  updateClockFace(colors, time, ledRotationOffset, br);

  for (int i=0; i<NUM_LEDS; i++) display.setPixel(i, colors[i]);
  display.show();
}

String IotsaNeoClockMod::info() {
  String rv = "<p>This is a NeoClock. See <a href='/neoclock'>/neoclock</a> (or <a href='/api/neoclock'>/api/neoclock</a>) for its alerts and configuration. ";
  rv += "Scripts use /alert?alert=name to play an alert, /status?color=0xrrggbb to set the main status ring, and ";
  rv += "/temporal?color=0xrrggbb&amp;factors=... to set the temporal status ring.</p>";
  return rv;
}

// A color as scripts write it: 0xrrggbb (or decimal).
static bool parseColor(const String& s, uint32_t& color) {
  if (s.length() == 0) return false;
  char *end;
  color = strtoul(s.c_str(), &end, 0);
  return *end == '\0';
}

// The temporal ring's factors as scripts write them: "1.0,0.5,...", up to
// 12; missing ones are 1.0.
static void parseFactors(const String& s, float factors[12]) {
  const char *p = s.c_str();
  for (int idx = 0; idx < 12; idx++) {
    float thisFactor = 1.0; // Default when the factors list runs out
    if (p && *p) {
      char *end;
      thisFactor = strtod(p, &end);
      if (isnan(thisFactor)) thisFactor = 0;
      p = end;
      if (*p == ',') p++;
    }
    factors[idx] = thisFactor;
  }
}

void IotsaNeoClockMod::setStatus(uint32_t color, uint32_t nextColor, uint32_t timeoutSecs) {
  currentStatusColor = color;
  nextStatusColor = nextColor;
  currentStatusColorEndTime = timeoutSecs ? millis() + timeoutSecs*1000 : 0;
}

void IotsaNeoClockMod::getHandler(IotsaApiContext& ctx) {
  ctx.show("ledRotationOffset", ledRotationOffset);
  ctx.show("currentAlert", currentAlertName);
  JsonArray alerts = ctx.reply()["availableAlerts"].to<JsonArray>();
  forEachAlertName([&alerts](const String &alertName) {
    alerts.add(alertName);
  });
}

// alert (a name, "" stops), status {color, nextColor, timeout} and
// temporal {color, factors: [12 numbers], timeout} do what the triggers do;
// colors are numbers here (0xrrggbb, JSON has no hexadecimal).
// ledRotationOffset is how the clock was built: it needs "config".
void IotsaNeoClockMod::putHandler(IotsaApiContext& ctx) {
  {
    IotsaContext::RightsScope rights(ctx);
    ctx.argsNeedRight("config");
    int newOffset = ledRotationOffset;
    if (ctx.arg("ledRotationOffset", newOffset, IotsaApiContext::Saved)) {
      ledRotationOffset = ((newOffset % 12) + 12) % 12;
    }
  }
  bool alert = ctx.argAction("alert", IotsaApiContext::Transient);
  bool status = ctx.argAction("status", IotsaApiContext::Transient);
  bool temporal = ctx.argAction("temporal", IotsaApiContext::Transient);
  JsonObject req = ctx.request();
  if (alert && !req["alert"].is<const char *>()) ctx.error(400, "alert: a name");
  if (status && !req["status"]["color"].is<uint32_t>()) ctx.error(400, "status: {\"color\": number, ...}");
  if (temporal && !req["temporal"]["color"].is<uint32_t>()) ctx.error(400, "temporal: {\"color\": number, ...}");
  if (ctx.failed()) return;
  if (alert) {
    String alertName = req["alert"].as<String>();
    if (alertName.length() == 0) {
      stopAlert();
    } else if (!startAlert(alertName)) {
      ctx.error(404, "Unknown alert " + alertName);
    }
  }
  if (status) {
    JsonObject s = req["status"];
    setStatus(s["color"], s["nextColor"] | 0, s["timeout"] | 0);
  }
  if (temporal) {
    JsonObject t = req["temporal"];
    float factors[12];
    JsonArray f = t["factors"];
    for (int idx = 0; idx < 12; idx++) {
      factors[idx] = (f && idx < (int)f.size()) ? f[idx].as<float>() : 1.0;
    }
    setTemporalStatus(t["color"], factors, t["timeout"] | 0);
  }
}

#ifdef IOTSA_WITH_WEB
void IotsaNeoClockMod::formHandler_args(IotsaWebContext& ctx) {
  if (ctx.argAction("stop", IotsaWebContext::Transient)) {
    stopAlert();
    ctx.feedback("Alert stopped.");
  }
  String alertName;
  if (ctx.arg("alert", alertName, IotsaWebContext::Transient)) {
    if (startAlert(alertName)) {
      ctx.feedback("Playing " + alertName + ".");
    } else {
      ctx.error(404, "Unknown alert " + alertName);
    }
  }
  IotsaContext::RightsScope rights(ctx);
  ctx.argsNeedRight("config");
  int newOffset = ledRotationOffset;
  if (ctx.arg("ledRotationOffset", newOffset, IotsaWebContext::Saved)) {
    ledRotationOffset = ((newOffset % 12) + 12) % 12;
  }
}

void IotsaNeoClockMod::formHandler_fields(IotsaWebContext& ctx) {
  if (ctx.includesEveryday()) {
    ctx.showStatus("Alert", "currentAlert", currentAlertName == "" ? String("none") : currentAlertName);
    ctx << "<h3>Alerts</h3>\n";
    forEachAlertName([&ctx](const String &alertName) {
      ctx.showButton("alert", alertName, IotsaWebContext::Transient, alertName.c_str());
    });
    if (currentAlertName != "") ctx.showButton("stop", "Stop", IotsaWebContext::Transient);
    ctx << "<p>Alert patterns are files uploaded on <a href='/upload'>/upload</a>, named \"" ALERT_FILE_PREFIX "&lt;name&gt;\" "
      "(e.g. \"" ALERT_FILE_PREFIX "Red\"). Scripts use /alert?alert=name and /alert?stop=1, "
      "/status?color=0xrrggbb[&amp;nextColor=0xrrggbb][&amp;timeout=seconds] and "
      "/temporal?color=0xrrggbb[&amp;factors=1.0,0.5,...][&amp;timeout=seconds].</p>\n";
  }
  if (ctx.includesSettings()) {
    IotsaContext::RightsScope rights(ctx);
    ctx.argsNeedRight("config");
    IotsaWebContext::Form form(ctx);
    ctx.showField("LED rotation offset", "ledRotationOffset", ledRotationOffset, IotsaWebContext::Saved, "0-11, the clock position where LED 0 sits, e.g. 1 if it is at 1 o'clock");
  }
}

void IotsaNeoClockMod::alertTrigger(IotsaWebContext& ctx) {
  // The status rings had their own URLs since 2026-07 (bfd9a02): say so.
  String moved;
  if (ctx.argPeek("status", moved) || ctx.argPeek("temporalStatus", moved)) {
    ctx.error("status and temporalStatus have moved: use /status?color=... and "
      "/temporal?color=...&factors=... instead of /alert?status=.../?temporalStatus=...");
    return;
  }
  bool stop = ctx.argAction("stop", IotsaWebContext::Transient);
  String alertName;
  bool start = ctx.arg("alert", alertName, IotsaWebContext::Transient);
  if (ctx.failed()) return;
  if (stop) {
    stopAlert();
  } else if (!start) {
    ctx.error("Usage: /alert?alert=name or /alert?stop=1. The alerts are listed on /neoclock.");
  } else if (!startAlert(alertName)) {
    ctx.error(404, "Unknown alert " + alertName);
  }
}

void IotsaNeoClockMod::statusTrigger(IotsaWebContext& ctx) {
  String color, nextColor;
  uint32_t timeoutSecs = 0;
  ctx.arg("color", color, IotsaWebContext::Transient);
  ctx.arg("nextColor", nextColor, IotsaWebContext::Transient);
  ctx.arg("timeout", timeoutSecs, IotsaWebContext::Transient);
  if (ctx.failed()) return;
  uint32_t c, n = 0;
  if (!parseColor(color, c) || (nextColor != "" && !parseColor(nextColor, n))) {
    ctx.error("Usage: /status?color=0xrrggbb[&nextColor=0xrrggbb][&timeout=seconds]");
    return;
  }
  setStatus(c, n, timeoutSecs);
}

void IotsaNeoClockMod::temporalTrigger(IotsaWebContext& ctx) {
  String color, factorsArg;
  uint32_t timeoutSecs = 0;
  ctx.arg("color", color, IotsaWebContext::Transient);
  ctx.arg("factors", factorsArg, IotsaWebContext::Transient);
  ctx.arg("timeout", timeoutSecs, IotsaWebContext::Transient);
  if (ctx.failed()) return;
  uint32_t c;
  if (!parseColor(color, c)) {
    ctx.error("Usage: /temporal?color=0xrrggbb[&factors=1.0,0.5,...][&timeout=seconds]");
    return;
  }
  float factors[12];
  parseFactors(factorsArg, factors);
  setTemporalStatus(c, factors, timeoutSecs);
}
#endif // IOTSA_WITH_WEB

void IotsaNeoClockMod::setTemporalStatus(uint32_t color, const float factors[12], uint32_t timeoutSecs) {
  int red = (color >> 16) & 0xff;
  int green = (color >> 8) & 0xff;
  int blue = color & 0xff;
  int startIdx = (ntpMod.localMinutes() / 5 + 1) % 12; // Where factors[0] will be displayed -- one segment ahead of the minute hand
  for (int idx = 0; idx < 12; idx++) {
    temporalStatusColor[(idx+startIdx)%12] = combineRGB(0, factors[idx], red, green, blue);
  }
  temporalStatusColorEndTime = timeoutSecs ? millis() + timeoutSecs*1000 : 0;
}

void IotsaNeoClockMod::setup() {
  display.begin();
  configLoad();
  // Identify (cwi-dis/iotsa#133): same two white flashes as IotsaLedMod's default
  // handler, via the status pulse channel -- render() already shows
  // iotsaStatus.statusColor() on the inner ring, so no rendering code is needed.
  IotsaRunmodeMod *runmode = IotsaRunmodeMod::instance();
  if (runmode) {
    runmode->addIdentifyCallback([]() { iotsaStatus.setStatusPulse(0xffffff, 300, 300, 1200, "identify"); });
  }
}

void IotsaNeoClockMod::lateSetup() {
  name = "neoclock";
  // /neoclock (the module's page) is registered by api.setup() below; /alert,
  // /status and /temporal are triggers, for scripts and other devices.
  api.setup("neoclock", true, true);
#ifdef IOTSA_WITH_WEB
  addTrigger("alert", [this](IotsaWebContext& ctx) { alertTrigger(ctx); });
  addTrigger("status", [this](IotsaWebContext& ctx) { statusTrigger(ctx); });
  addTrigger("temporal", [this](IotsaWebContext& ctx) { temporalTrigger(ctx); });
#endif
}

void IotsaNeoClockMod::configLoad() {
  IotsaConfigFileLoad cf("/config/neoclock.cfg");
  cf.get("ledRotationOffset", ledRotationOffset, 0);
}

void IotsaNeoClockMod::configSave() {
  IotsaConfigFileSave cf("/config/neoclock.cfg");
  cf.put("ledRotationOffset", ledRotationOffset);
}

void IotsaNeoClockMod::loop() {
  render();
}
