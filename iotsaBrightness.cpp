#include "iotsaBrightness.h"
#include "iotsaConfigFile.h"

void
IotsaBrightnessMod::handler() {
  bool anyChanged = false;
  for (uint8_t i=0; i<server->args(); i++) {
    if (server->argName(i) == "brightness") {
      int b = server->arg(i).toInt();
      if (b > 0 && b/100.0 != maxBrightnessFactor) {
        maxBrightnessFactor = b/100.0;
        curBrightnessFactor = maxBrightnessFactor;
        anyChanged = true;
      }
    }
#ifdef WITH_ADAPTATION
    if (server->argName(i) == "auto") {
      String a = server->arg(i);
      bool na = false;
      if (a == "yes" || a == "true" || a.toInt() > 0) na = true;
      if (na != autoBrightness) {
      	autoBrightness = na;
      	anyChanged = true;
      }
    }
    if (server->argName(i) == "minBrightness") {
      int b = server->arg(i).toInt();
      if (b > 0 && b/100.0 != minBrightnessFactor) {
        minBrightnessFactor = b/100.0;
        anyChanged = true;
      }
    }
#endif
  }
  if (anyChanged) configSave();

  String message = "<html><head><title>Brightness</title></head><body><h1>Brightness</h1><form>";
#ifdef WITH_ADAPTATION
  if (autoBrightness) message += "(Maximum) ";
#endif
  message += "Brightness (%): <input type='range' min='1' max='100' value='";
  message += String(int(maxBrightnessFactor*100));
  message += "' name='brightness'><br>";
#ifdef WITH_ADAPTATION
  message += "Adapt to ambient light:<br><input name='auto' type='radio' value='no'";
  if (!autoBrightness) message += " checked";
  message += "> No<br><input name='auto' type='radio' value='yes'";
  if (autoBrightness) message += " checked";
  message += "> Yes, Minimum Brightness (%): <input type='range' min='1' max='100' value='";
  message += String(int(minBrightnessFactor*100));
  message += "' name='minBrightness'><br>";
  message += "Brightness at current light level: ";
  message += String(int(brightness()*100));
  message += "%<br>";
#endif
  message += "<input type='submit'></form></body></html>";
  server->send(200, "text/html", message);
}

void IotsaBrightnessMod::setup() {
  pinMode(A0, INPUT);
  configLoad();
}

void IotsaBrightnessMod::serverSetup() {
  server->on("/brightness", std::bind(&IotsaBrightnessMod::handler, this));
}

void IotsaBrightnessMod::configLoad() {
  IotsaConfigFileLoad cf("/config/brightness.cfg");
  cf.get("brightnessFactor", maxBrightnessFactor, 1.0);
  curBrightnessFactor = maxBrightnessFactor;
#ifdef WITH_ADAPTATION
  int ab;
  cf.get("autoBrightness", ab, 0);
  cf.get("minBrightnessFactor", minBrightnessFactor, 0.0);
  autoBrightness = (bool)ab;
#endif
 
}

void IotsaBrightnessMod::configSave() {
  IotsaConfigFileSave cf("/config/brightness.cfg");
  cf.put("brightnessFactor", maxBrightnessFactor);
#ifdef WITH_ADAPTATION
  cf.put("autoBrightness", int(autoBrightness));
  cf.put("minBrightnessFactor", minBrightnessFactor);
#endif
}

void IotsaBrightnessMod::loop() {
#ifdef WITH_ADAPTATION
  static uint32_t notBefore;
  if (millis() < notBefore) return;
  notBefore = millis() + 100;
  int light = analogRead(A0);
  float newLightLevel = light/1024.0;
  lightLevel = ((DECAY-1)*lightLevel+newLightLevel)/DECAY;
  if (lightLevel <= 0) lightLevel = 0.001;
  if (lightLevel > 1) lightLevel = 1;
  if (autoBrightness) curBrightnessFactor = minBrightnessFactor + lightLevel * (maxBrightnessFactor-minBrightnessFactor);
  if (curBrightnessFactor < 0) curBrightnessFactor = 0;
  if (curBrightnessFactor > 1) curBrightnessFactor = 1;
#endif
}

String IotsaBrightnessMod::info() {
  String rv = "<p>Brightness currently ";
  rv += String(int(curBrightnessFactor*100));
  rv += "%";
#ifdef WITH_ADAPTATION
  if (autoBrightness) {
    rv += " (adapting to ambient light, min ";
    rv += String(int(minBrightnessFactor*100));
    rv += "% max ";
    rv += String(int(maxBrightnessFactor*100));
    rv += "%)";
  }
#endif
#ifdef WITH_ADAPTATION
  rv += ", ambient light level ";
  rv += String(int(lightLevel*100));
  rv += "%";
#endif
  rv += ". See <a href='/brightness'>/brightness</a> to change.</p>";
  return rv;
}

float IotsaBrightnessMod::brightness() {
  return curBrightnessFactor;
}
