//
// Boilerplate for configurable web server (probably RESTful) running on ESP8266.
//
// Virgin servers come up with a private WiFi network, as host 192.168.4.1.
// You can set SSID, Password and hostname through a web interface.
// Then the server comes up on that network with the given hostname (.local)
// And you can access the applications.
//
// Look for the string CHANGE to see where you need to add things for your application.
//

#include <Esp.h>
#include <FS.h>
#include "iotsa.h"
#include "iotsaWifi.h"
#include "iotsaNtp.h"
#include "iotsaOta.h"
#include "iotsaFiles.h"
#include "iotsaFilesUpload.h"
#include "iotsaFilesBackup.h"
#include "iotsaSimple.h"

#define IFDEBUGX if(0)

IotsaApplication application("NeoPixel Clock Server");

// Configure modules we need
IotsaWifiMod wifiMod(application);  // wifi is always needed
IotsaNtpMod ntpMod(application);    // we want NTP because we're a clock
IotsaOtaMod otaMod(application);    // we want OTA for updating the software without removing the clock
IotsaFilesMod filesMod(application);// we want files mainly to share notification patterns
IotsaFilesUploadMod filesUploadMod(application);  // we want upload to set notification patterns
IotsaFilesBackupMod filesBackupMod(application);  // we want backup to clone the clock

//
// NeoClock handler
//
#include <Adafruit_NeoPixel.h>

#define NEOPIXEL_PIN      14
#define NUM_LEDS          60
#define FIRST_SEC_LED     0
#define FIRST_MIN_LED     0
#define FIRST_HOUR_LED    2
#define NUM_HOUR_LEDS     3
#define STRIDE_HOUR_LEDS  5
#define NUM_MIN_LEDS      5
#define STRIDE_MIN_LEDS   5

#undef FIRST_LED_AT_ONE_O_CLOCK // Define for first two clocks, which had first led at 1 o'clock position
#define WITH_BRIGHTNESS   // Define to allow changing of the clock brightness through the web interface

#ifdef WITH_BRIGHTNESS
#include "iotsaBrightness.h"
IotsaBrightnessMod brightnessMod(application);
#define BRIGHTNESS() brightnessMod.brightness()
#else
#define BRIGHTNESS() (0.19)
#endif

#define COLOR_SEC 0x555555
#define COLOR_MIN 0x555500
#define COLOR_HOUR 0x005500

Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

//
// Helper function - combine r/g/b values into rgb with clamping and checking
//
static uint32_t combineRGB(uint32_t oldRGB, float factor, int red, int green, int blue) {
  red = (int)(red*factor) + int(red > 0);
  green = (int)(green*factor) + int(green > 0);
  blue = (int)(blue*factor) + int(blue > 0);
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
// Helper function - blend color into pixel
//
static void blendPixel(int idx, float factor, uint32_t rgb) {
  int red = (rgb >> 16) & 0xff;
  int green = (rgb >> 8) & 0xff;
  int blue = rgb & 0xff;
  factor = factor * BRIGHTNESS();
  uint32_t oldColor = strip.getPixelColor(idx);
  strip.setPixelColor(idx, combineRGB(oldColor, factor, red, green, blue));
}

void neoClockSetup() {
  strip.begin();
}

void neoClockShowSeconds(int seconds, float frac) {
  float valBefore = frac < 0.3 ? (0.3-frac) : 0;
  float valAfter = frac > 0.7 ?  (frac-0.7) : 0;
  float valSelf = 1.0-(valBefore+valAfter);
  IFDEBUGX { IotsaSerial.printf("seconds=%d, before=%f, self=%f, after=%f", seconds, valBefore, valSelf, valAfter); IotsaSerial.println(); }

  int ledSelf = FIRST_SEC_LED+seconds;
  int ledBefore = ledSelf + NUM_LEDS-1;
  int ledAfter = ledSelf + 1;
  if (ledBefore >= NUM_LEDS) ledBefore -= NUM_LEDS;
  if (ledSelf >= NUM_LEDS) ledSelf -= NUM_LEDS;
  if (ledAfter >= NUM_LEDS) ledAfter -= NUM_LEDS;
  if (valBefore) {
    blendPixel(ledBefore, valBefore, COLOR_SEC);
  }
  if (valSelf) {
    blendPixel(ledSelf, valSelf, COLOR_SEC);
  }
  if (valAfter) {
    blendPixel(ledAfter, valAfter, COLOR_SEC);
  }
}

void neoClockShowMinutes(int minutes, float frac) {
  int minMod5 = minutes % 5;
  minutes = minutes / 5;
  IFDEBUGX { IotsaSerial.print("minutes="); IotsaSerial.println(minutes); }
  int firstLed = FIRST_MIN_LED + (minutes*STRIDE_MIN_LEDS);
  if (firstLed >= NUM_LEDS) firstLed -= NUM_LEDS;

  // Set all the pixels corresponding to the minute hand
  for (int i=firstLed; i<firstLed+NUM_MIN_LEDS; i++) {
    blendPixel(i, 1.0, COLOR_MIN);
  }
  // And set the one corresponding to the current minute again, to highlight it
  blendPixel(firstLed+minMod5, 1.0, COLOR_MIN);
}

void neoClockShowHours(int hours, float frac) {
  IFDEBUGX { IotsaSerial.print("hours="); IotsaSerial.println(hours); }
  int firstLed = FIRST_HOUR_LED;
#ifdef FIRST_LED_AT_ONE_O_CLOCK
  firstLed += (hours+11)*STRIDE_HOUR_LEDS;
#else
  firstLed += hours*STRIDE_HOUR_LEDS;
#endif
  int extraLed = 0;
  // Determine which segment to show the hour hand (depending on whether where
  // at <30 minutes past the hour or >30 minutes past) and
  // which of the three hour-hand leds to highlight
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
    blendPixel(i, 1.0, COLOR_HOUR);
  }
  // And highlight the pixel corresponding to where in the half hour we are
  blendPixel(firstLed+extraLed, 1.0, COLOR_HOUR);
}

unsigned long lastTimeShown;

File currentAlert;
bool currentAlertIsBinary;
long currentAlertLineEndTime;

uint32_t read32bin(Stream& s) {
  int b1 = s.read();
  int b2 = s.read();
  int b3 = s.read();
  int b4 = s.read();
  if (b4 < 0) return -1;
  return (b1<<24) | (b2 << 16) | (b3 << 8) | b4;
}

bool neoClockShowAlert() {
  if (!currentAlert) return false;
  // We need to show an alert, or possibly read a new line from the alert file.
  // Note that alerts are not blended but override the clock and status.
  // Alerts are also not dependent on brightness.
  if (millis() > currentAlertLineEndTime) {
    if (currentAlertIsBinary) {
      // Binary data. Read bigendian 32bit integers duration and count.
      int nextDuration = read32bin(currentAlert);
      int nextCount = read32bin(currentAlert);
      if (nextDuration < 0 || nextCount < 0) {
        currentAlert.close();
        return false;
      }
      currentAlertLineEndTime = millis() + nextDuration;
      int currentLed = 0;
      while (currentLed < nextCount) {
        int rgb = read32bin(currentAlert);
        strip.setPixelColor(currentLed, rgb);
        currentLed++;
      }
    } else {
      // Ascii data. Duration, space, space-separated R, G, B values until end of line.
      int nextDuration = currentAlert.parseInt();
      if (nextDuration <= 0) {
        currentAlert.close();
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
        strip.setPixelColor(currentLed, r, g, b);
        currentLed++;
      }
    }
  }
  strip.show();
  return true;
}

uint32_t currentStatusColor;
uint32_t currentStatusColorEndTime;
uint32_t nextStatusColor;

bool neoClockShowStatus() {
  if (currentStatusColorEndTime && millis() > currentStatusColorEndTime) {
    // Clear the status indicator whenever it times out
    currentStatusColorEndTime = 0;
    currentStatusColor = nextStatusColor;
  }
  uint32_t color = 0;
  color = iotsaConfig.getStatusColor();
  if (color == 0) {
    color = currentStatusColor;
  }
  if (color == 0) return false;
  // Set the inner ring to the status color
  for(int i=STRIDE_HOUR_LEDS-1; i<NUM_LEDS; i+=STRIDE_HOUR_LEDS) {
    blendPixel(i, 1.0, color);
  }
  return true;
}

class NeoClockIotsaStatus : public IotsaStatusInterface {
  void showStatus() {
    strip.clear();
    neoClockShowStatus();
    strip.show();
  }
};

NeoClockIotsaStatus iotsaStatus;

uint32_t temporalStatusColor[12];
uint32_t temporalStatusColorEndTime;

bool neoClockShowTemporalStatus() {
  if (temporalStatusColorEndTime && millis() > temporalStatusColorEndTime) {
    // Clear the status indicator whenever it times out
    temporalStatusColorEndTime = 0;
    for (int i=0; i<12; i++) temporalStatusColor[i] = 0;
  }
  // Set the outer ring to the per-5-minutes status color
  int idx;
  for(int i=0; i<NUM_LEDS; i+=STRIDE_HOUR_LEDS) {
    blendPixel(i, 1.0, temporalStatusColor[idx++]);
  }
  return true;
}

void neoClockShowTime() {
  // First see whether we're showing an alert, and return if we are (after showing it)
  if (neoClockShowAlert()) return;
  // And for the clock, only update at most 20 times per second
  if (millis() < lastTimeShown + 50) return;
  lastTimeShown = millis();
  strip.clear();
  neoClockShowStatus();
  neoClockShowTemporalStatus();
  int h = ntpMod.localHours12(), m=ntpMod.localMinutes(), s=ntpMod.localSeconds();
  neoClockShowSeconds(s, (millis()%1000)/1000.0);
  neoClockShowMinutes(m, s/60.0);
  neoClockShowHours(h, m/60.0);
  strip.show();
}

String
neoClockInfo() {
  // To be done
  return "<p>This is a NeoClock. Alerts are available via <a href='/alert'>/alert</a>.</p>";
}

bool neoClockStartAlert(String &name) {
  if (currentAlert) {
    currentAlert.close();
  }
  String fileName = "/data/" + name;
  currentAlert = SPIFFS.open(fileName, "r");
  if (!currentAlert) return false;
  currentAlertIsBinary = fileName.endsWith(".bin");
  return true;
}

void neoClockAlert() {
  uint32_t endTime = 0;
  for (uint8_t i=0; i<application.server->args(); i++) {
    if (application.server->argName(i) == "timeout") {
      String toutstr = application.server->arg(i);
      endTime = strtoul(toutstr.c_str(), 0, 0);
      endTime = endTime*1000 + millis();
    }
    if( application.server->argName(i) == "alert") {
      String alertName = application.server->arg(i);
      if (neoClockStartAlert(alertName)) {
        application.server->send(200, "text/plain", "OK");
      } else {
        application.server->send(404, "text/plain", "Unknown alert " + alertName);
      }
      return;
    }
    if (application.server->argName(i) == "status") {
      String color = application.server->arg(i);
      char *nextColor = NULL;
      currentStatusColor = strtoul(color.c_str(), &nextColor, 0);
      if (*nextColor == '/') {
        // Color to show after the timeout is after the slash
        nextColor++;
        nextStatusColor = strtoul(nextColor, 0, 0);
      } else {
        nextStatusColor = 0;
      }
      currentStatusColorEndTime = endTime; // Note: timeout argument must come before status argument
      application.server->send(200, "text/plain", "OK");
      return;
    }
    if (application.server->argName(i) == "temporalStatus") {
      String argStr = application.server->arg(i);
      char *argCStr = (char *)argStr.c_str();
      // Read base color value
      uint32_t color = strtoul(argCStr, &argCStr, 0);
      int red = (color >> 16) & 0xff;
      int green = (color >> 8) & 0xff;
      int blue = color & 0xff;
      int idx = 0;  // For 0 to 11, number of values we re expecting (maximum)
      int startIdx = ntpMod.localMinutes() / 5; // Where value[0] will be displayed
      for (int idx = 0; idx < 12; idx++) {
        float thisFactor = 1.0; // What we should multiply the color with, for this LED        
        if(  argCStr && *argCStr == '/') {
          // If there are more factors we read them, otherwise we use 1.0
          argCStr++;
          thisFactor = strtod(argCStr, &argCStr);
          if (isnan(thisFactor)) thisFactor = 0;
        }
        temporalStatusColor[(idx+startIdx)%12] = combineRGB(0, thisFactor, red, green, blue);
      }
      temporalStatusColorEndTime = endTime;
      application.server->send(200, "text/plain", "OK");
      return;
    }
  }
  String message = "<html><head><title>Available Alerts</title></head><body><h1>Available Alerts</h1><ul>";
  Dir d = SPIFFS.openDir("/data");
  while (d.next()) {
      String alertName = d.fileName().substring(6);
      String alertUserName = alertName;
      if (alertUserName.endsWith(".bin")) {
        alertUserName.remove(alertUserName.length()-4);
      }
      message += "<li><a href='/alert?alert=" + IotsaMod::htmlEncode(alertName) + "'>" + IotsaMod::htmlEncode(alertUserName) + "</a></li>";
  }
  message += "</ul><p>To show status access /alert?timeout=seconds&status=0xrrggbb/0xrrggbb, to show temporal status access /alert?temporalStatus=0xrrggbb/1.0/0.5/...</p></body></html>";
  application.server->send(200, "text/html", message);
}

void neoClockLoop() {
  neoClockShowTime();
}

IotsaSimpleMod neoMod(application, "/alert", neoClockAlert, neoClockInfo);

void setup(void){
  neoClockSetup();
  application.status = &iotsaStatus;
  application.setup();
  application.serverSetup();
  ESP.wdtEnable(WDTO_120MS);
}
 
void loop(void){
  application.loop();
  neoClockLoop();
} 
