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

#ifdef IOTSA_WITH_API
#define IotsaBuienradarModBaseMod IotsaApiMod
#else
#define IotsaBuienradarModBaseMod IotsaMod
#endif

// Color used for the rain-forecast ring (dim blue, matching the dimness of
// the other ring colors in iotsaNeoClockMod.h).
#define COLOR_RAIN 0x000055

// Poll interval -- matches buienradar's own 5-minute forecast granularity,
// and the "reasonable, non-abusive interval" conclusion from #7's investigation.
#define BUIENRADAR_POLL_INTERVAL_MS (5UL*60UL*1000UL)

// Timeout passed to setTemporalStatus(), slightly longer than the poll
// interval: a single missed/failed poll doesn't blank the ring immediately,
// but the ring still clears itself (rather than showing a stale forecast
// forever) if the next poll doesn't land either.
#define BUIENRADAR_STATUS_TIMEOUT_SECS (6UL*60UL)

// Rain intensity (mm/h) that maps to a full-brightness (factor 1.0) ring segment.
#define BUIENRADAR_MAX_INTENSITY_MMH 5.0

// IotsaRequest::send() verifies TLS differently per platform (see
// iotsaRequest.cpp): ESP32 pins the issuing root CA (WiFiClientSecure::
// setCACert()), ESP8266 pins the exact leaf certificate's SHA1 fingerprint
// (BearSSL::WiFiClientSecure::setFingerprint()) -- a cheaper check (no X509
// chain parsing), but one that breaks on every certificate renewal, not just
// a CA change. sslInfo's meaning (and this default) therefore differs by
// platform; the /buienradar form's field label follows suit.
#ifdef ESP32
// Root CA for gadgets.buienradar.nl, as of 2026-08-18: DigiCert Global Root G3
// (valid to 2038-01-15). Overridable via the /buienradar config form if
// buienradar.nl ever switches CAs before this default is updated.
#define BUIENRADAR_SSLINFO_LABEL "Root CA certificate"
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
#else
// SHA1 fingerprint of gadgets.buienradar.nl's current leaf certificate, as of
// 2026-08-18 -- that certificate is only valid until 2027-01-29, so unlike
// the ESP32 root CA above, THIS DEFAULT WILL EXPIRE and need updating (via
// the /buienradar config form, or a new firmware build) around then, and
// again at every renewal after that. See cwi-dis/iotsaNeoClock#7.
#define BUIENRADAR_SSLINFO_LABEL "Certificate fingerprint (SHA1)"
#define BUIENRADAR_DEFAULT_SSLINFO "1B:05:3E:50:00:10:3D:7D:82:DD:F9:C5:0A:04:FE:01:FE:7E:98:8D"
#endif

// Fetches the buienradar.nl rain forecast for a configured lat/lon and drives
// the clock's outer (temporal status) ring via IotsaNeoClockMod::setTemporalStatus().
// See cwi-dis/iotsaNeoClock#7.
class IotsaBuienradarMod : public IotsaBuienradarModBaseMod {
public:
  IotsaBuienradarMod(IotsaApplication &_app, IotsaNeoClockMod &_neoClockMod, IotsaAuthMod *_auth=NULL)
  : IotsaBuienradarModBaseMod(_app, _auth),
    neoClockMod(_neoClockMod),
    enabled(true),
    latitude(0.0),
    longitude(0.0),
    nextPollTime(0)
  {}

  void setup() override;
  void serverSetup() override;
  void loop() override;
  String info() override;
protected:
#ifdef IOTSA_WITH_API
  bool getHandler(const char *path, JsonObject& reply) override;
  bool putHandler(const char *path, const JsonVariant& request, JsonObject& reply) override;
#endif
  void configLoad() override;
  void configSave() override;
private:
  void handler(); // /buienradar -- module configuration (latitude, longitude, rootCA)
  void poll();    // fetch, parse and apply the current forecast

  IotsaNeoClockMod &neoClockMod;
  IotsaRequest req; // url is fixed in configLoad(); sslInfo doubles as the configurable rootCA
  bool enabled; // field kill-switch, in case the live fetch misbehaves
  float latitude;
  float longitude;
  uint32_t nextPollTime;
};

#endif
