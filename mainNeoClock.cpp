//
// An internet-connected clock, comprised of 60 NeoPixel LEDs. Shows the time,
// but can also show programmable patterns (as alerts) and temporal information
// (such as expected rainfall for the coming hour). See readme.md.
//

#include "iotsa.h"
#include "iotsaFS.h"
#include "iotsaWifi.h"
#include "iotsaNtp.h"
#include "iotsaOta.h"
#include "iotsaFiles.h"
#include "iotsaFilesUpload.h"
#include "iotsaFilesBackup.h"

IotsaApplication application("NeoPixel Clock Server");

// Configure modules we need
IotsaWifiMod wifiMod(application);  // wifi is always needed
IotsaNtpMod ntpMod(application);    // we want NTP because we're a clock
IotsaOtaMod otaMod(application);    // we want OTA for updating the software without removing the clock
IotsaFilesMod filesMod(application);// we want files mainly to share notification patterns
IotsaFilesUploadMod filesUploadMod(application);  // we want upload to set notification patterns
IotsaFilesBackupMod filesBackupMod(application);  // we want backup to clone the clock

#define NEOPIXEL_PIN      14

#define WITH_BRIGHTNESS   // Define to allow changing of the clock brightness through the web interface

#ifdef WITH_BRIGHTNESS
#include "iotsaBrightnessMod.h"
IotsaBrightnessMod brightnessMod(application);
#endif

#include "iotsaNeoClockMod.h"

#ifdef WITH_NEOPIXEL_DISPLAY
#include "NeoPixelStripDisplay.h"
NeoPixelStripDisplay neoClockDisplay(NUM_LEDS, NEOPIXEL_PIN);
#endif
#ifdef WITH_ROUNDLCD_DISPLAY
#include "CrowPanelRoundDisplay.h"
CrowPanelRoundDisplay neoClockDisplay(NUM_LEDS);
#endif

IotsaNeoClockMod neoClockMod(application, neoClockDisplay,
#ifdef WITH_BRIGHTNESS
  &brightnessMod
#else
  NULL
#endif
);

#ifdef WITH_BUIENRADAR
#include "iotsaBuienradarMod.h"
IotsaBuienradarMod buienradarMod(application, neoClockMod);
#endif

void setup(void){
  application.status = &neoClockMod;
  application.setup();
  application.lateSetup();
#ifndef ESP32
  ESP.wdtEnable(WDTO_120MS);
#endif
}

void loop(void){
  application.loop();
}
