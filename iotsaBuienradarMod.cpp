#include "iotsaBuienradarMod.h"
#include "iotsaConfigFile.h"

// buienradar.nl's raintext endpoint: 24 lines of "LLL|HH:MM", one per 5-minute
// step covering the next 2 hours. We only need the first 12 (the next hour) --
// that's all setTemporalStatus()'s 12-segment ring can show. LLL is a
// logarithmic rain-intensity level (mm/h = 10^((LLL-109)/32), per buienradar's
// docs); we map the level itself, not mm/h, onto the ring -- see
// BUIENRADAR_LEVEL_OFF/_FULL.
//
// Note the trailing slash and the 2-decimal-place lat/lon below: the endpoint
// 302-redirects any request with more than 2 decimal places to a canonical
// URL truncated to its actual grid resolution, 0.01 degrees (~1.1km) --
// matching what igor's old pull.sh already truncated to. IotsaRequest doesn't
// follow redirects, so requesting more precision than that fails outright.
#define BUIENRADAR_URL "https://gadgets.buienradar.nl/data/raintext/"

// IotsaConfigFileLoad/Save's config files are one "name=value" per line, with
// '\n' as the record separator -- a multi-line PEM stored verbatim gets cut
// down to just its first line on save (silently corrupting it, since the
// remaining lines are then misread as further name=value entries that don't
// match anything). So the root CA is escaped to a single line for storage.
// Workaround for cwi-dis/iotsa#197; remove once that's fixed -- tracked in
// cwi-dis/iotsaNeoClock#8.
static String escapeNewlines(const String &s) {
  String r = s;
  r.replace("\n", "\\n");
  return r;
}

static String unescapeNewlines(const String &s) {
  String r = s;
  r.replace("\\n", "\n");
  return r;
}

void IotsaBuienradarMod::poll() {
  char query[32];
  snprintf(query, sizeof(query), "lat=%.2f&lon=%.2f", latitude, longitude);
  IFDEBUG {
    IotsaSerial.print("IotsaBuienradarMod: GET ");
    IotsaSerial.print(BUIENRADAR_URL);
    IotsaSerial.print("?");
    IotsaSerial.println(query);
    IotsaSerial.print("IotsaBuienradarMod: free heap before request: ");
    IotsaSerial.println(ESP.getFreeHeap());
  }
  String body;
  bool ok = req.send(query, &body);
  IFDEBUG {
    IotsaSerial.print("IotsaBuienradarMod: free heap after request: ");
    IotsaSerial.println(ESP.getFreeHeap());
  }
  lastPollAtMillis = millis();
  if (!ok) {
    IFDEBUG IotsaSerial.println("IotsaBuienradarMod: request failed");
    lastPollStatus = "request failed";
    return;
  }
  int n = 0;
  int lineStart = 0;
  while (n < 12 && lineStart < (int)body.length()) {
    int lineEnd = body.indexOf('\n', lineStart);
    if (lineEnd < 0) lineEnd = body.length();
    int barIdx = body.indexOf('|', lineStart);
    if (barIdx > 0 && barIdx < lineEnd) {
      lastLevels[n] = body.substring(lineStart, barIdx).toInt();
      String time = body.substring(barIdx + 1, lineEnd);
      time.trim();
      strncpy(lastTimes[n], time.c_str(), sizeof(lastTimes[n]) - 1);
      lastTimes[n][sizeof(lastTimes[n]) - 1] = '\0';
      n++;
    }
    lineStart = lineEnd + 1;
  }
  lastCount = n;
  if (n < 12) {
    IFDEBUG IotsaSerial.println("IotsaBuienradarMod: short/unparseable response, ignoring");
    lastPollStatus = "short response";
    return;
  }
  lastPollStatus = "ok";
  applyLevels();
}

void IotsaBuienradarMod::applyLevels() {
  if (lastCount < 12) return;
  float maxFactor = 0;
  for (int i = 0; i < 12; i++) {
    // Clamping to exactly 0 matters: any factor > 0 trips combineRGB()'s
    // round-up-to-1 floor and shows faint dots, including for level 0
    // (buienradar's explicit "no rain").
    float factor = (float)(lastLevels[i] - levelOff) / (levelFull - levelOff);
    if (factor < 0.0) factor = 0.0;
    if (factor > 1.0) factor = 1.0;
    lastFactors[i] = factor;
    if (factor > maxFactor) maxFactor = factor;
  }
  IFDEBUG {
    IotsaSerial.print("IotsaBuienradarMod: OK, max factor ");
    IotsaSerial.print(maxFactor);
    IotsaSerial.println(maxFactor > 0 ? " (rain expected)" : " (no rain expected)");
  }
  neoClockMod.setTemporalStatus(COLOR_RAIN, lastFactors, BUIENRADAR_STATUS_TIMEOUT_SECS);
}

void IotsaBuienradarMod::loop() {
  if (!enabled) return;
  if (latitude == 0.0 && longitude == 0.0) return; // not configured yet
  if (WiFi.status() != WL_CONNECTED) return;
  if (millis() < nextPollTime) return;
  nextPollTime = millis() + BUIENRADAR_POLL_INTERVAL_MS;
  poll();
}

// Everything here is a setting of the clock itself: changing needs "config"
// (lateSetup()). The coordinates also give away where the clock hangs, the
// home address: reading them needs "config" too. The two levels are tuning,
// how the forecast looks: "lighttuning".
template<class Ctx> bool IotsaBuienradarMod::_args(Ctx& ctx) {
  bool changed = ctx.arg("enabled", enabled, IotsaContext::Saved);
  IotsaContext::RightsScope rights(ctx);
  ctx.showsNeedRight("config");
  changed |= ctx.arg("latitude", latitude, IotsaContext::Saved);
  changed |= ctx.arg("longitude", longitude, IotsaContext::Saved);
  return changed;
}

template<class Ctx> bool IotsaBuienradarMod::_levelArgs(Ctx& ctx) {
  IotsaContext::RightsScope rights(ctx);
  ctx.argsNeedRight("lighttuning");
  int newOff = levelOff;
  int newFull = levelFull;
  bool changed = ctx.arg("levelOff", newOff, IotsaContext::Saved);
  changed |= ctx.arg("levelFull", newFull, IotsaContext::Saved);
  if (changed && newFull <= newOff) ctx.error(400, "levelFull must be above levelOff");
  if (!changed || ctx.failed()) return false;
  levelOff = newOff;
  levelFull = newFull;
  return true;
}

void IotsaBuienradarMod::getHandler(IotsaApiContext& ctx) {
  ctx.show("enabled", enabled);
  {
    IotsaContext::RightsScope rights(ctx);
    ctx.showsNeedRight("config");
    ctx.show("latitude", latitude);
    ctx.show("longitude", longitude);
  }
  ctx.show("levelOff", levelOff);
  ctx.show("levelFull", levelFull);
  if (lastPollAtMillis) {
    JsonObject last = ctx.reply()["lastPoll"].to<JsonObject>();
    last["secondsAgo"] = (millis() - lastPollAtMillis) / 1000;
    last["status"] = lastPollStatus;
    JsonArray times = last["times"].to<JsonArray>();
    JsonArray levels = last["levels"].to<JsonArray>();
    JsonArray factors = last["factors"].to<JsonArray>();
    for (int i = 0; i < lastCount; i++) {
      times.add(lastTimes[i]);
      levels.add(lastLevels[i]);
      if (lastCount == 12) factors.add(lastFactors[i]);
    }
  }
}

void IotsaBuienradarMod::putHandler(IotsaApiContext& ctx) {
  bool changed = _args(ctx);
  bool levelsChanged = _levelArgs(ctx);
  if (ctx.failed()) return;
  if (changed) nextPollTime = 0; // re-poll promptly with the new settings
  if (levelsChanged) applyLevels(); // no new poll needed: the levels are kept
}

#ifdef IOTSA_WITH_WEB
void IotsaBuienradarMod::formHandler_args(IotsaWebContext& ctx) {
  bool changed = _args(ctx);
  bool levelsChanged = _levelArgs(ctx);
  // Never prefilled: empty leaves it as it is, "clear" goes back to the
  // compiled-in default.
  String newCA = req.sslInfo;
  if (ctx.argPassword("rootCA", newCA, IotsaWebContext::Saved, false, true)) {
    req.sslInfo = newCA.length() ? newCA : String(BUIENRADAR_DEFAULT_SSLINFO);
    changed = true;
  }
  if (ctx.failed()) return;
  if (changed) nextPollTime = 0;
  if (levelsChanged) applyLevels();
}

void IotsaBuienradarMod::formHandler_fields(IotsaWebContext& ctx) {
  if (ctx.includesEveryday()) {
    String status = !enabled ? "disabled" : (latitude == 0.0 && longitude == 0.0) ? "not configured" : "polling every 5 minutes";
    ctx.showStatus("Rain forecast", "", status);
    if (lastPollAtMillis == 0) {
      ctx.showStatus("Last poll", "lastPoll", "none yet");
      return;
    }
    ctx.showStatus("Last poll", "lastPoll", String((millis() - lastPollAtMillis) / 1000) + " seconds ago, " + lastPollStatus);
    if (lastCount == 0) return;
    // What the ring shows and why: from where the minute hand points, one
    // row per 5 minutes.
    ctx << "<table><tr><th>Time</th><th>Level</th><th>Factor</th></tr>\n";
    for (int i = 0; i < lastCount; i++) {
      ctx << "<tr><td>" + htmlEncode(lastTimes[i]) + "</td><td>" + String(lastLevels[i]) + "</td><td>"
        + (lastCount == 12 ? String(lastFactors[i], 2) : String("")) + "</td></tr>\n";
    }
    ctx << "</table>\n";
    return;
  }
  {
    IotsaWebContext::Form form(ctx);
    ctx.showCheckbox("Enabled", "enabled", enabled, IotsaWebContext::Saved, "kill switch, in case the live fetch misbehaves");
    IotsaContext::RightsScope rights(ctx);
    ctx.showsNeedRight("config");
    ctx.showField("Latitude", "latitude", latitude, IotsaWebContext::Saved, "right-click your location on Google Maps and copy the two numbers");
    ctx.showField("Longitude", "longitude", longitude, IotsaWebContext::Saved);
  }
  {
    IotsaContext::RightsScope rights(ctx);
    ctx.argsNeedRight("lighttuning");
    IotsaWebContext::Form form(ctx, "Save", "Rain levels");
    ctx.showField("Off at", "levelOff", levelOff, IotsaWebContext::Saved, "this level and below shows nothing; 40 is about 0.007 mm/h");
    ctx.showField("Full at", "levelFull", levelFull, IotsaWebContext::Saved, "this level and above is full brightness; 144 is about 12 mm/h");
  }
  bool isDefault = req.sslInfo == String(BUIENRADAR_DEFAULT_SSLINFO);
  IotsaWebContext::Form form(ctx, "Save", "Root CA certificate");
  ctx.showStatus("Root CA", "", isDefault ? "the compiled-in default (DigiCert Global Root G3)" : "set here");
  ctx.showTextarea("New root CA (PEM)", "rootCA", IotsaWebContext::Saved, "advanced: only when buienradar.nl's certificate issuer changes", !isDefault);
}
#endif // IOTSA_WITH_WEB

void IotsaBuienradarMod::setup() {
  configLoad();
}

void IotsaBuienradarMod::lateSetup() {
  name = "buienradar";
  api.setup("buienradar", true, true);
  changesNeedRight("config");
}

void IotsaBuienradarMod::configLoad() {
  IotsaConfigFileLoad cf("/config/buienradar.cfg");
  int en;
  cf.get("enabled", en, 1);
  enabled = (bool)en;
  cf.get("latitude", latitude, 0.0f);
  cf.get("longitude", longitude, 0.0f);
  cf.get("levelOff", levelOff, BUIENRADAR_LEVEL_OFF);
  cf.get("levelFull", levelFull, BUIENRADAR_LEVEL_FULL);
  if (levelFull <= levelOff) {
    levelOff = BUIENRADAR_LEVEL_OFF;
    levelFull = BUIENRADAR_LEVEL_FULL;
  }
  String storedSslInfo;
  cf.get("rootCA", storedSslInfo, "");
  storedSslInfo = unescapeNewlines(storedSslInfo);
  // Self-heals an sslInfo that was saved before the escaping above existed
  // (silently truncated to just its first line, a bare
  // "-----BEGIN CERTIFICATE-----" with nothing after it) by falling back to
  // the compiled-in default whenever the stored value isn't valid-looking PEM.
  bool looksValid = storedSslInfo.startsWith("-----BEGIN CERTIFICATE-----") && storedSslInfo.indexOf("-----END CERTIFICATE-----") > 0;
  req.sslInfo = looksValid ? storedSslInfo : String(BUIENRADAR_DEFAULT_SSLINFO);
  req.url = BUIENRADAR_URL;
}

void IotsaBuienradarMod::configSave() {
  IotsaConfigFileSave cf("/config/buienradar.cfg");
  cf.put("enabled", int(enabled));
  cf.put("latitude", latitude);
  cf.put("longitude", longitude);
  cf.put("levelOff", levelOff);
  cf.put("levelFull", levelFull);
  cf.put("rootCA", escapeNewlines(req.sslInfo));
}

String IotsaBuienradarMod::info() {
  String rv = "<p>Rain forecast (buienradar.nl) ";
  if (!enabled) {
    rv += "disabled";
  } else if (latitude == 0.0 && longitude == 0.0) {
    rv += "not configured";
  } else {
    rv += "configured";  // not the coordinates: they give away where the clock hangs
  }
  rv += ". See <a href='/buienradar'>/buienradar</a> to change, or <a href='/api/buienradar'>/api/buienradar</a> for the REST API.</p>";
  return rv;
}
