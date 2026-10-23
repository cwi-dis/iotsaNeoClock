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
  if (!ok) {
    IFDEBUG IotsaSerial.println("IotsaBuienradarMod: request failed");
    return;
  }
  float factors[12];
  int nFactors = 0;
  int lineStart = 0;
  while (nFactors < 12 && lineStart < (int)body.length()) {
    int lineEnd = body.indexOf('\n', lineStart);
    if (lineEnd < 0) lineEnd = body.length();
    int barIdx = body.indexOf('|', lineStart);
    if (barIdx > 0 && barIdx < lineEnd) {
      int level = body.substring(lineStart, barIdx).toInt();
      // Clamping to exactly 0 matters: any factor > 0 trips combineRGB()'s
      // round-up-to-1 floor and shows faint dots, including for level 0
      // (buienradar's explicit "no rain").
      float factor = (float)(level - BUIENRADAR_LEVEL_OFF) / (BUIENRADAR_LEVEL_FULL - BUIENRADAR_LEVEL_OFF);
      if (factor < 0.0) factor = 0.0;
      if (factor > 1.0) factor = 1.0;
      factors[nFactors++] = factor;
    }
    lineStart = lineEnd + 1;
  }
  if (nFactors < 12) {
    IFDEBUG IotsaSerial.println("IotsaBuienradarMod: short/unparseable response, ignoring");
    return;
  }
  float maxFactor = 0;
  for (int i = 0; i < 12; i++) if (factors[i] > maxFactor) maxFactor = factors[i];
  IFDEBUG {
    IotsaSerial.print("IotsaBuienradarMod: OK, max factor ");
    IotsaSerial.print(maxFactor);
    IotsaSerial.println(maxFactor > 0 ? " (rain expected)" : " (no rain expected)");
  }
  neoClockMod.setTemporalStatus(COLOR_RAIN, factors, BUIENRADAR_STATUS_TIMEOUT_SECS);
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
// home address: reading them needs "config" too.
template<class Ctx> bool IotsaBuienradarMod::_args(Ctx& ctx) {
  bool changed = false;
  changed |= ctx.arg("enabled", enabled, IotsaContext::Saved);
  ctx.showsNeedRight("config");
  changed |= ctx.arg("latitude", latitude, IotsaContext::Saved);
  changed |= ctx.arg("longitude", longitude, IotsaContext::Saved);
  return changed;
}

void IotsaBuienradarMod::getHandler(IotsaApiContext& ctx) {
  ctx.show("enabled", enabled);
  ctx.showsNeedRight("config");
  ctx.show("latitude", latitude);
  ctx.show("longitude", longitude);
}

void IotsaBuienradarMod::putHandler(IotsaApiContext& ctx) {
  if (_args(ctx)) nextPollTime = 0; // re-poll promptly with the new settings
}

#ifdef IOTSA_WITH_WEB
void IotsaBuienradarMod::formHandler_args(IotsaWebContext& ctx) {
  bool changed = _args(ctx);
  // Never prefilled: empty leaves it as it is, "clear" goes back to the
  // compiled-in default.
  String newCA = req.sslInfo;
  if (ctx.argPassword("rootCA", newCA, IotsaWebContext::Saved, false, true)) {
    req.sslInfo = newCA.length() ? newCA : String(BUIENRADAR_DEFAULT_SSLINFO);
    changed = true;
  }
  if (changed && !ctx.failed()) nextPollTime = 0;
}

void IotsaBuienradarMod::formHandler_fields(IotsaWebContext& ctx) {
  if (ctx.includesEveryday()) {
    String status = !enabled ? "disabled" : (latitude == 0.0 && longitude == 0.0) ? "not configured" : "polling every 5 minutes";
    ctx.showStatus("Rain forecast", "", status);
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
