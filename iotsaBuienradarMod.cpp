#include "iotsaBuienradarMod.h"
#include "iotsaConfigFile.h"
#include <math.h>

// buienradar.nl's raintext endpoint: 24 lines of "LLL|HH:MM", one per 5-minute
// step covering the next 2 hours. We only need the first 12 (the next hour) --
// that's all setTemporalStatus()'s 12-segment ring can show. LLL is a
// logarithmic rain-intensity level, converted to mm/h via buienradar's own
// documented formula.
//
// Note the trailing slash and the 2-decimal-place lat/lon below: the endpoint
// 302-redirects any request with more than 2 decimal places to a canonical
// URL truncated to its actual grid resolution, 0.01 degrees (~1.1km) --
// matching what igor's old pull.sh already truncated to. IotsaRequest doesn't
// follow redirects, so requesting more precision than that fails outright.
#define BUIENRADAR_URL "https://gadgets.buienradar.nl/data/raintext/"
#define BUIENRADAR_LEVEL_TO_MMH(level) (powf(10.0, ((level)-109)/32.0))

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
      // level 0 is buienradar's explicit "no rain" sentinel, not just the
      // bottom of the log scale -- the formula below asymptotes toward but
      // never reaches 0 mm/h, which previously left a near-zero-but-nonzero
      // factor that still tripped combineRGB()'s round-up-to-1 floor.
      float factor = 0.0;
      if (level > 0) {
        float mmh = BUIENRADAR_LEVEL_TO_MMH(level);
        factor = mmh / BUIENRADAR_MAX_INTENSITY_MMH;
        if (factor > 1.0) factor = 1.0;
      }
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

void IotsaBuienradarMod::handler() {
  bool anyChanged = false;
  if (server->hasArg("enabled")) {
    String e = server->arg("enabled");
    bool ne = (e == "yes" || e == "true" || e.toInt() > 0);
    if (ne != enabled) { enabled = ne; anyChanged = true; }
  }
  if (server->hasArg("latitude")) {
    float v = server->arg("latitude").toFloat();
    if (v != latitude) { latitude = v; anyChanged = true; }
  }
  if (server->hasArg("longitude")) {
    float v = server->arg("longitude").toFloat();
    if (v != longitude) { longitude = v; anyChanged = true; }
  }
  if (server->hasArg("rootCA")) {
    String v;
    IotsaMod::percentDecode(server->arg("rootCA"), v);
    if (v != req.sslInfo) { req.sslInfo = v; anyChanged = true; }
  }
  if (anyChanged) {
    configSave();
    nextPollTime = 0; // re-poll promptly with the new settings
  }

  String message = "<html><head><title>Buienradar rain forecast</title></head><body><h1>Buienradar rain forecast</h1><form method='get'>";
  message += "Enabled:<br><input name='enabled' type='radio' value='yes'";
  if (enabled) message += " checked";
  message += "> Yes<br><input name='enabled' type='radio' value='no'";
  if (!enabled) message += " checked";
  message += "> No <i>(kill switch, in case the live fetch misbehaves)</i><br>\n";
  message += "Latitude: <input name='latitude' value='" + String(latitude, 6) + "'><br>\n";
  message += "Longitude: <input name='longitude' value='" + String(longitude, 6) + "'><br>\n";
  message += "<p>Find your coordinates by right-clicking your location on <a href='https://maps.google.com' target='_blank'>Google Maps</a> and copying the two numbers shown.</p>\n";
  message += BUIENRADAR_SSLINFO_LABEL " <i>(advanced -- only change if buienradar.nl's certificate changes)</i>:<br>\n";
  message += "<textarea name='rootCA' rows='10' cols='70'>" + req.sslInfo + "</textarea><br>\n";
  message += "<input type='submit'></form></body></html>";
  server->send(200, "text/html", message);
}

#ifdef IOTSA_WITH_API
bool IotsaBuienradarMod::getHandler(const char *path, JsonObject& reply) {
  reply["enabled"] = enabled;
  reply["latitude"] = latitude;
  reply["longitude"] = longitude;
  return true;
}

bool IotsaBuienradarMod::putHandler(const char *path, const JsonVariant& request, JsonObject& reply) {
  bool anyChanged = false;
  JsonObject reqObj = request.as<JsonObject>();
  if (getFromRequest<bool, bool>(reqObj, "enabled", enabled)) anyChanged = true;
  if (getFromRequest<float, float>(reqObj, "latitude", latitude)) anyChanged = true;
  if (getFromRequest<float, float>(reqObj, "longitude", longitude)) anyChanged = true;
  if (anyChanged) {
    configSave();
    nextPollTime = 0;
  }
  return anyChanged;
}
#endif // IOTSA_WITH_API

void IotsaBuienradarMod::setup() {
  configLoad();
}

void IotsaBuienradarMod::serverSetup() {
  server->on("/buienradar", std::bind(&IotsaBuienradarMod::handler, this));
#ifdef IOTSA_WITH_API
  api.setup("/api/buienradar", true, true);
  name = "buienradar";
#endif
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
  // (silently truncated to just its first line -- on ESP32 that's a bare
  // "-----BEGIN CERTIFICATE-----" with nothing after it; a fingerprint on
  // ESP8266 is a single line already, so it was never affected) by falling
  // back to the compiled-in default whenever the stored value doesn't look
  // like a complete one.
#ifdef ESP32
  bool looksValid = storedSslInfo.startsWith("-----BEGIN CERTIFICATE-----") && storedSslInfo.indexOf("-----END CERTIFICATE-----") > 0;
#else
  bool looksValid = storedSslInfo.length() == 59; // "XX:XX:...:XX", 20 hex bytes
#endif
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
    rv += "for " + String(latitude, 6) + "," + String(longitude, 6);
  }
  rv += ". See <a href='/buienradar'>/buienradar</a> to change, or <a href='/api/buienradar'>/api/buienradar</a> for the REST API.</p>";
  return rv;
}
