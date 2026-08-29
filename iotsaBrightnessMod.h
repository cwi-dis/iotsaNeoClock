#ifndef _IOTSABRIGHTNESS_H_
#define _IOTSABRIGHTNESS_H_
#include "iotsa.h"
#include "iotsaApi.h"

//
// Define to adjust light level based on ambient light,
// read from LDR connected to ADC input.
// Decay determines how fast the light level reacts to
// changes in ambient light.
//
#define WITH_ADAPTATION
#define DECAY 40

class IotsaBrightnessMod : public IotsaModule {
public:
  IotsaBrightnessMod(IotsaApplication &_app, IotsaAuthMod *_auth=NULL)
  :	IotsaModule(_app, _auth),
  curBrightnessFactor(1.0),
  maxBrightnessFactor(1.0)
#ifdef WITH_ADAPTATION
  , autoBrightness(false),
  minBrightnessFactor(0.0),
  lightLevel(1.0)
#endif
  {}

  void setup() override;
  void lateSetup() override;
  void loop() override;
  String info() override;
  float brightness();
protected:
  bool getHandler(const char *path, JsonObject& reply) override;
  bool putHandler(const char *path, const JsonVariant& request, JsonObject& reply) override;
  void webHandler() override;
  void configLoad() override;
  void configSave() override;
  float curBrightnessFactor;
  float maxBrightnessFactor;
#ifdef WITH_ADAPTATION
  bool autoBrightness;
  float minBrightnessFactor;
  float lightLevel;
#endif
};

#endif
