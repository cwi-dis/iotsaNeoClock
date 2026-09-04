#include "iotsaNeoClockMod.h"
#include "iotsaConfigFile.h"

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
      blendPixel(colors, i, 1.0, COLOR_MIN, brightness);
    }
    // And set the one corresponding to the current minute again, to highlight it
    blendPixel(colors, firstLed+minMod5, 1.0, COLOR_MIN, brightness);
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
      blendPixel(colors, i, 1.0, COLOR_HOUR, brightness);
    }
    // And highlight the pixel corresponding to where in the half hour we are
    blendPixel(colors, firstLed+extraLed, 1.0, COLOR_HOUR, brightness);
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

void IotsaNeoClockMod::showStatus() {
  // Called by the framework to show boot/config-mode status (no clock/alert rendering).
  display.clear();
  StatusState statusState = { iotsaStatus.statusColor() };
  uint32_t tmpColors[NUM_LEDS];
  for (int i=0; i<NUM_LEDS; i++) tmpColors[i] = 0;
  updateStatus(tmpColors, statusState, 1.0);
  for (int i=0; i<NUM_LEDS; i++) display.setPixel(i, tmpColors[i]);
  display.show();
}

String IotsaNeoClockMod::info() {
  String rv = "<p>This is a NeoClock. See <a href='/neoclock'>/neoclock</a> (or <a href='/api/neoclock'>/api/neoclock</a>) for configuration, ";
  rv += "<a href='/alert'>/alert</a> to trigger/list alert patterns, ";
  rv += "<a href='/status'>/status</a> to set the main status ring, and ";
  rv += "<a href='/temporal'>/temporal</a> to set the temporal status ring.</p>";
  return rv;
}

bool IotsaNeoClockMod::getHandler(const char *path, JsonObject& reply) {
  reply["ledRotationOffset"] = ledRotationOffset;
  reply["currentAlert"] = currentAlertName;
  JsonArray alerts = reply["availableAlerts"].to<JsonArray>();
  forEachAlertName([&alerts](const String &alertName) {
    alerts.add(alertName);
  });
  return true;
}

bool IotsaNeoClockMod::putHandler(const char *path, const JsonVariant& request, JsonObject& reply) {
  JsonObject reqObj = request.as<JsonObject>();

  bool configChanged = false;
  int newOffset;
  if (getFromRequest<int, int>(reqObj, "ledRotationOffset", newOffset)) {
    newOffset = ((newOffset % 12) + 12) % 12;
    if (newOffset != ledRotationOffset) {
      ledRotationOffset = newOffset;
      configChanged = true;
    }
  }
  if (configChanged) configSave();

  bool anyChanged = configChanged;
  String newAlert;
  if (getFromRequest<const char *, String>(reqObj, "alert", newAlert)) {
    // Not persisted -- an alert name in a PUT request always triggers or
    // stops playback immediately, same as /alert?alert=name over HTTP.
    if (newAlert.length() == 0) {
      stopAlert();
    } else {
      startAlert(newAlert);
    }
    anyChanged = true;
  }
  return anyChanged;
}

void IotsaNeoClockMod::webHandler() {
  // /neoclock -- module configuration, following the normal iotsa web-form convention.
  bool anyChanged = false;
  if (api.webService->server->hasArg("ledRotationOffset")) {
    int newOffset = ((api.webService->server->arg("ledRotationOffset").toInt() % 12) + 12) % 12;
    if (newOffset != ledRotationOffset) {
      ledRotationOffset = newOffset;
      anyChanged = true;
    }
  }
  if (anyChanged) configSave();

  String message = "<html><head><title>NeoClock Configuration</title></head><body><h1>NeoClock Configuration</h1><form>";
  message += "LED rotation offset (0-11, the clock position where LED 0 actually sits, e.g. 1 if LED 0 is at the 1 o'clock spot): ";
  message += "<input name='ledRotationOffset' value='";
  message += String(ledRotationOffset);
  message += "'><br><input type='submit'></form></body></html>";
  api.webService->server->send(200, "text/html", message);
}

void IotsaNeoClockMod::alertHandler() {
  // /alert -- trigger a named alert pattern, or list the available ones.
  if (app.server->hasArg("status") || app.server->hasArg("temporalStatus")) {
    app.server->send(400, "text/plain",
      "status and temporalStatus have moved: use /status?color=... and "
      "/temporal?color=...&factors=... instead of /alert?status=.../?temporalStatus=...");
    return;
  }
  if (app.server->hasArg("stop")) {
    stopAlert();
    app.server->send(200, "text/plain", "OK");
    return;
  }
  if (app.server->hasArg("alert")) {
    String alertName = app.server->arg("alert");
    if (startAlert(alertName)) {
      app.server->send(200, "text/plain", "OK");
    } else {
      app.server->send(404, "text/plain", "Unknown alert " + alertName);
    }
    return;
  }
  String message = "<html><head><title>Available Alerts</title></head><body><h1>Available Alerts</h1><ul>";
  forEachAlertName([&message](const String &alertName) {
    message += "<li><a href='/alert?alert=" + IotsaBaseModule::htmlEncode(alertName) + "'>" + IotsaBaseModule::htmlEncode(alertName) + "</a></li>";
  });
  message += "</ul><p>To trigger: /alert?alert=name. To stop: /alert?stop=1. "
    "Alert files are uploaded via <a href='/upload'>/upload</a>, named \"" ALERT_FILE_PREFIX "&lt;name&gt;\" "
    "(e.g. \"" ALERT_FILE_PREFIX "Red\"). See <a href='/status'>/status</a> and <a href='/temporal'>/temporal</a> "
    "for the status rings.</p></body></html>";
  app.server->send(200, "text/html", message);
}

void IotsaNeoClockMod::statusHandler() {
  // /status -- set the main (inner ring) status indicator.
  if (!app.server->hasArg("color")) {
    app.server->send(200, "text/plain", "Usage: /status?color=0xrrggbb[&nextColor=0xrrggbb][&timeout=seconds]");
    return;
  }
  currentStatusColor = strtoul(app.server->arg("color").c_str(), NULL, 0);
  nextStatusColor = app.server->hasArg("nextColor") ? strtoul(app.server->arg("nextColor").c_str(), NULL, 0) : 0;
  uint32_t timeoutSecs = app.server->hasArg("timeout") ? app.server->arg("timeout").toInt() : 0;
  currentStatusColorEndTime = timeoutSecs ? millis() + timeoutSecs*1000 : 0;
  app.server->send(200, "text/plain", "OK");
}

void IotsaNeoClockMod::temporalStatusHandler() {
  // /temporal -- set the temporal (outer ring, per-5-minute-segment) status indicator.
  if (!app.server->hasArg("color")) {
    app.server->send(200, "text/plain", "Usage: /temporal?color=0xrrggbb[&factors=1.0,0.5,...][&timeout=seconds]");
    return;
  }
  uint32_t color = strtoul(app.server->arg("color").c_str(), NULL, 0);
  String factorsArg = app.server->hasArg("factors") ? app.server->arg("factors") : "";
  char *factorsCStr = (char *)factorsArg.c_str();
  float factors[12];
  for (int idx = 0; idx < 12; idx++) {
    float thisFactor = 1.0; // Default when the factors list runs out
    if (factorsCStr && *factorsCStr) {
      thisFactor = strtod(factorsCStr, &factorsCStr);
      if (isnan(thisFactor)) thisFactor = 0;
      if (*factorsCStr == ',') factorsCStr++;
    }
    factors[idx] = thisFactor;
  }
  uint32_t timeoutSecs = app.server->hasArg("timeout") ? app.server->arg("timeout").toInt() : 0;
  setTemporalStatus(color, factors, timeoutSecs);
  app.server->send(200, "text/plain", "OK");
}

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
}

void IotsaNeoClockMod::lateSetup() {
  name = "neoclock";
  // /neoclock (the module's own config page) is registered by api.setup() below.
  // /alert, /status and /temporal are extra routes outside that page, so they're
  // registered directly on the shared web server.
  app.server->on("/alert", std::bind(&IotsaNeoClockMod::alertHandler, this));
  app.server->on("/status", std::bind(&IotsaNeoClockMod::statusHandler, this));
  app.server->on("/temporal", std::bind(&IotsaNeoClockMod::temporalStatusHandler, this));
  api.setup("neoclock", true, true);
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
