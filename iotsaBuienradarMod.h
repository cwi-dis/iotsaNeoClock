#ifndef _IOTSABUIENRADARMOD_H_
#define _IOTSABUIENRADARMOD_H_
#include "iotsa.h"
#include "iotsaApi.h"
#include "iotsaRequest.h"
#include "iotsaNeoClockMod.h"
#ifdef ESP32
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#endif

// Color used for the rain-forecast ring: the teal igor's neoclock plugin used,
// restored because the pure-blue 0x000055 that replaced it looked too dark (#11).
#define COLOR_RAIN 0x206060

// Poll interval -- matches buienradar's own 5-minute forecast granularity,
// and the "reasonable, non-abusive interval" conclusion from #7's investigation.
#define BUIENRADAR_POLL_INTERVAL_MS (5UL*60UL*1000UL)

// Timeout passed to setTemporalStatus(), slightly longer than the poll
// interval: a single missed/failed poll doesn't blank the ring immediately,
// but the ring still clears itself (rather than showing a stale forecast
// forever) if the next poll doesn't land either.
#define BUIENRADAR_STATUS_TIMEOUT_SECS (6UL*60UL)

// Rain level -> ring-segment factor, linear in buienradar's logarithmic level
// (not in mm/h), as igor's neoclock plugin did: level*0.01-0.4, clamped to 0..1.
// Level 40 (~0.007 mm/h) and below is off, level 140 (~9 mm/h) is full
// brightness, so light rain (~0.3 mm/h) already shows at about half (#11).
// The defaults of the levelOff/levelFull settings, which can be tuned on
// /buienradar while watching the last poll's levels and factors.
#define BUIENRADAR_LEVEL_OFF  40
#define BUIENRADAR_LEVEL_FULL 140

// Root CA for gadgets.buienradar.nl, as of 2026-08-18: DigiCert Global Root G3
// (valid to 2038-01-15). Overridable via the /buienradar config form if
// buienradar.nl ever switches CAs before this default is updated. Since
// cwi-dis/iotsa#198, IotsaRequest::send() pins the issuing root CA on both
// ESP32 and ESP8266 (previously ESP8266 pinned the exact leaf certificate's
// fingerprint instead -- cheaper, but broke on every certificate renewal).
#define BUIENRADAR_DEFAULT_SSLINFO \
"-----BEGIN CERTIFICATE-----\n" \
"MIICPzCCAcWgAwIBAgIQBVVWvPJepDU1w6QP1atFcjAKBggqhkjOPQQDAzBhMQsw\n" \
"CQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3d3cu\n" \
"ZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBHMzAe\n" \
"Fw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVTMRUw\n" \
"EwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5jb20x\n" \
"IDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEczMHYwEAYHKoZIzj0CAQYF\n" \
"K4EEACIDYgAE3afZu4q4C/sLfyHS8L6+c/MzXRq8NOrexpu80JX28MzQC7phW1FG\n" \
"fp4tn+6OYwwX7Adw9c+ELkCDnOg/QW07rdOkFFk2eJ0DQ+4QE2xy3q6Ip6FrtUPO\n" \
"Z9wj/wMco+I+o0IwQDAPBgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAd\n" \
"BgNVHQ4EFgQUs9tIpPmhxdiuNkHMEWNpYim8S8YwCgYIKoZIzj0EAwMDaAAwZQIx\n" \
"AK288mw/EkrRLTnDCgmXc/SINoyIJ7vmiI1Qhadj+Z4y3maTD/HMsQmP3Wyr+mt/\n" \
"oAIwOWZbwmSNuJ5Q3KjVSaLtx9zRSX8XAbjIho9OjIgrqJqpisXRAL34VOKa5Vt8\n" \
"sycX\n" \
"-----END CERTIFICATE-----\n"

// Fetches the buienradar.nl rain forecast for a configured lat/lon and drives
// the clock's outer (temporal status) ring via IotsaNeoClockMod::setTemporalStatus().
// See cwi-dis/iotsaNeoClock#7.
class IotsaBuienradarMod : public IotsaModule {
public:
  IotsaBuienradarMod(IotsaApplication &_app, IotsaNeoClockMod &_neoClockMod)
  : IotsaModule(_app),
    neoClockMod(_neoClockMod),
    enabled(true),
    latitude(0.0),
    longitude(0.0),
    // Delay the first poll by a full interval after boot, rather than firing
    // immediately: gives NTP time to sync first (CA verification needs a
    // correctly-set clock, unlike the old fingerprint pinning -- an
    // immediate poll can otherwise race NTP and fail once on "not yet
    // valid"), and gives a window to disable the feature via /buienradar
    // before it ever makes a network attempt.
    nextPollTime(BUIENRADAR_POLL_INTERVAL_MS)
  {}

  void setup() override;
  void lateSetup() override;
  void loop() override;
  String info() override;
protected:
  void getHandler(IotsaApiContext& ctx) override;
  void putHandler(IotsaApiContext& ctx) override;
#ifdef IOTSA_WITH_WEB
  String webTitle() override { return "Buienradar rain forecast"; }
  void formHandler_args(IotsaWebContext& ctx) override;   // /buienradar: enabled, latitude, longitude, rootCA
  void formHandler_fields(IotsaWebContext& ctx) override;
#endif
  // enabled, latitude and longitude, for both contexts: true if changed.
  template<class Ctx> bool _args(Ctx& ctx);
  // levelOff and levelFull ("lighttuning"): true if changed.
  template<class Ctx> bool _levelArgs(Ctx& ctx);
  void configLoad() override;
  void configSave() override;
private:
  void poll();    // fetch, parse and apply the current forecast
  // The last poll's levels to factors, with levelOff/levelFull, and the
  // factors to the ring. Again after a level setting changed.
  void applyLevels();

  IotsaNeoClockMod &neoClockMod;
  IotsaRequest req; // url is fixed in configLoad(); sslInfo doubles as the configurable rootCA
  bool enabled; // field kill-switch, in case the live fetch misbehaves
  float latitude;
  float longitude;
  uint32_t nextPollTime;
  // Level -> factor: levelOff and below is off, levelFull and above is full.
  int levelOff = BUIENRADAR_LEVEL_OFF;
  int levelFull = BUIENRADAR_LEVEL_FULL;
  // The last poll, to see what the ring shows and why: per segment of the
  // coming hour the time buienradar gives, its level and the factor.
  uint32_t lastPollAtMillis = 0;      // 0: no poll yet
  const char *lastPollStatus = nullptr; // "ok", "request failed", "short response"
  int lastCount = 0;                  // segments parsed, at most 12
  char lastTimes[12][6];              // "HH:MM"
  int lastLevels[12];
  float lastFactors[12];
};

#endif
