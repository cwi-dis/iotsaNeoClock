#include "iotsaBrightnessMod.h"
#include "iotsaConfigFile.h"

// How bright the clock is, the everyday knob, is open; how it follows the
// light in the room is tuning, as a lamp's settings are ("lighttuning").
template<class Ctx> void IotsaBrightnessMod::_args(Ctx& ctx) {
  float newMax = maxBrightnessFactor;
  bool maxChanged = ctx.arg("brightnessFactor", newMax, IotsaContext::Saved);
  IotsaContext::RightsScope rights(ctx);
  ctx.argsNeedRight("lighttuning");
#ifdef WITH_ADAPTATION
  float newMin = minBrightnessFactor;
  bool minChanged = ctx.arg("minBrightnessFactor", newMin, IotsaContext::Saved);
  ctx.arg("autoBrightness", autoBrightness, IotsaContext::Saved);
#endif
  if (maxChanged && (newMax <= 0 || newMax > 1)) ctx.error(400, "brightnessFactor: more than 0, at most 1");
#ifdef WITH_ADAPTATION
  if (minChanged && (newMin < 0 || newMin > 1)) ctx.error(400, "minBrightnessFactor: 0 to 1");
#endif
  if (ctx.failed()) return;
  if (maxChanged) {
    maxBrightnessFactor = newMax;
    curBrightnessFactor = maxBrightnessFactor;
  }
#ifdef WITH_ADAPTATION
  if (minChanged) minBrightnessFactor = newMin;
#endif
}

void IotsaBrightnessMod::getHandler(IotsaApiContext& ctx) {
  ctx.show("brightnessFactor", maxBrightnessFactor);
#ifdef WITH_ADAPTATION
  ctx.show("autoBrightness", autoBrightness);
  ctx.show("minBrightnessFactor", minBrightnessFactor);
#endif
}

void IotsaBrightnessMod::putHandler(IotsaApiContext& ctx) {
  _args(ctx);
}

#ifdef IOTSA_WITH_WEB
void IotsaBrightnessMod::formHandler_args(IotsaWebContext& ctx) {
  _args(ctx);
}

void IotsaBrightnessMod::formHandler_fields(IotsaWebContext& ctx) {
  if (ctx.includesEveryday()) {
    IotsaWebContext::Form form(ctx, "Set");
#ifdef WITH_ADAPTATION
    const char *hint = autoBrightness ? "0.0 to 1.0, the maximum while adapting" : "0.0 to 1.0";
#else
    const char *hint = "0.0 to 1.0";
#endif
    ctx.showField("Brightness", "brightnessFactor", maxBrightnessFactor, IotsaWebContext::Saved, hint);
#ifdef WITH_ADAPTATION
    ctx.showStatus("Brightness now", "", String(int(brightness()*100)) + "%");
#endif
  }
#ifdef WITH_ADAPTATION
  if (ctx.includesSettings()) {
    IotsaContext::RightsScope rights(ctx);
    ctx.argsNeedRight("lighttuning");
    IotsaWebContext::Form form(ctx, "Save", "Adapting to the light in the room");
    ctx.showCheckbox("Adapt to ambient light", "autoBrightness", autoBrightness, IotsaWebContext::Saved);
    ctx.showField("Minimum brightness", "minBrightnessFactor", minBrightnessFactor, IotsaWebContext::Saved, "0.0 to 1.0, in the dark");
  }
#endif
}
#endif // IOTSA_WITH_WEB

void IotsaBrightnessMod::setup() {
  pinMode(A0, INPUT);
  configLoad();
}

void IotsaBrightnessMod::lateSetup() {
  name = "brightness";
  api.setup("brightness", true, true);
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
  rv += ". See <a href='/brightness'>/brightness</a> to change, or <a href='/api/brightness'>/api/brightness</a> for the REST API.</p>";
  return rv;
}

float IotsaBrightnessMod::brightness() {
  return curBrightnessFactor;
}
