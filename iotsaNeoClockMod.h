#ifndef _IOTSANEOCLOCKMOD_H_
#define _IOTSANEOCLOCKMOD_H_
#include <Esp.h>
#include <FS.h>
#include <functional>
#include "iotsa.h"
#include "iotsaApi.h"
#include "iotsaFS.h"
#include "iotsaNtp.h"
#include "NeoClockDisplay.h"
#include "iotsaBrightnessMod.h"

extern IotsaNtpMod ntpMod;

#define NUM_LEDS          60
#define FIRST_SEC_LED     0
#define FIRST_MIN_LED     0
#define FIRST_HOUR_LED    2
#define NUM_HOUR_LEDS     3
#define STRIDE_HOUR_LEDS  5
#define NUM_MIN_LEDS      5
#define STRIDE_MIN_LEDS   5

#define COLOR_SEC 0x555555
#define COLOR_MIN 0x555500
#define COLOR_HOUR 0x005500

// Current time, in the form the clock-face renderer needs it.
struct ClockTime {
  int hours;    // 0-11
  int minutes;  // 0-59
  int seconds;  // 0-59
  float frac;   // fractional second, 0.0-1.0
};

// Main status ring (inner ring, one uniform color).
struct StatusState {
  uint32_t color;
};

// Temporal status ring (outer ring, one color per 5-minute segment).
struct TemporalStatusState {
  uint32_t colors[12];
};

class IotsaNeoClockMod : public IotsaModule, public IotsaStatusInterface {
public:
  IotsaNeoClockMod(IotsaApplication &_app, NeoClockDisplay &_display, IotsaBrightnessMod *_brightnessMod=NULL)
  : IotsaModule(_app),
    display(_display),
    brightnessMod(_brightnessMod),
    ledRotationOffset(0),
    lastTimeShown(0),
    currentAlertLineEndTime(0),
    currentStatusColor(0),
    currentStatusColorEndTime(0),
    nextStatusColor(0),
    temporalStatusColorEndTime(0)
  {
    for (int i=0; i<NUM_LEDS; i++) colors[i] = 0;
    for (int i=0; i<12; i++) temporalStatusColor[i] = 0;
  }
  void setup() override;
  void lateSetup() override;
  void loop() override;
  String info() override;
  void showStatus() override; // IotsaStatusInterface, for boot/config-mode feedback
  // Set the outer per-5-minute-segment ring directly -- shared by the /temporal
  // HTTP handler and other in-process data providers (e.g. IotsaBuienradarMod, #7)
  // that want to drive it without a self-request round trip.
  void setTemporalStatus(uint32_t color, const float factors[12], uint32_t timeoutSecs=0);
protected:
  bool getHandler(const char *path, JsonObject& reply) override;
  bool putHandler(const char *path, const JsonVariant& request, JsonObject& reply) override;
  void webHandler() override;   // /neoclock -- module configuration (ledRotationOffset)
  void configLoad() override;
  void configSave() override;
private:
  void alertHandler();          // /alert -- trigger/list alert patterns
  void statusHandler();         // /status -- set the inner status ring
  void temporalStatusHandler(); // /temporal -- set the outer per-segment ring
  void render();  // recompute colors[] and push it to the display
  bool startAlert(const String &name);
  void stopAlert();
  bool renderAlert();  // true if an alert is currently active (and was rendered)
  // Alert pattern files share iotsa's generic /data upload directory with
  // arbitrary other files, so they're distinguished by an ALERT_FILE_PREFIX
  // filename prefix rather than by assuming everything in /data is an alert
  // (see cwi-dis/iotsaNeoClock#5) -- cb is called once per alert, with the
  // prefix already stripped.
  void forEachAlertName(std::function<void(const String &alertName)> cb);
  float brightness();

  // Data providers: each blends its own contribution into colors[NUM_LEDS].
  // Deliberately narrow (explicit struct in, buffer in/out) rather than
  // reaching into arbitrary object state, so each is independently testable
  // and -- longer-term idea -- could be swapped for a scriptable equivalent
  // without changing this module's own structure.
  static void updateClockFace(uint32_t colors[NUM_LEDS], const ClockTime &time, int ledRotationOffset, float brightness);
  static void updateStatus(uint32_t colors[NUM_LEDS], const StatusState &state, float brightness);
  static void updateTemporalStatus(uint32_t colors[NUM_LEDS], const TemporalStatusState &state, int ledRotationOffset, float brightness);

  static void blendPixel(uint32_t colors[NUM_LEDS], int idx, float factor, uint32_t rgb, float brightness);
  static uint32_t combineRGB(uint32_t oldRGB, float factor, int red, int green, int blue);

  NeoClockDisplay &display;
  IotsaBrightnessMod *brightnessMod;
  uint32_t colors[NUM_LEDS];

  // Runtime-configurable replacement for the old FIRST_LED_AT_ONE_O_CLOCK
  // compile-time flag -- see cwi-dis/iotsaNeoClock#2. Value is the clock
  // position (0-11) where LED 0 actually sits: 0 for clocks with the first
  // LED at 12 o'clock, 1 for the first two clocks built (first LED at the
  // 1 o'clock spot).
  int ledRotationOffset;

  unsigned long lastTimeShown;

  File currentAlert;
  String currentAlertName;  // "" when no alert is playing
  unsigned long currentAlertLineEndTime;

  uint32_t currentStatusColor;
  uint32_t currentStatusColorEndTime;
  uint32_t nextStatusColor;

  uint32_t temporalStatusColor[12];
  uint32_t temporalStatusColorEndTime;
};

#endif
