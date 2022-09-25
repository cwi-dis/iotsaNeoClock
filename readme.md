# iotsaNeoClock - an internet-connected clock

![build-platformio](https://github.com/cwi-dis/iotsaNeoClock/workflows/build-platformio/badge.svg)
![build-arduino](https://github.com/cwi-dis/iotsaNeoClock/workflows/build-arduino/badge.svg)

A clock comprised of 60 NeoPixel LEDs. Shows the time, but can also show programmable patterns (as alerts) and temporal information (such as expected rainfall for the coming hour). Schematics and building instructions included.

Home page is <https://github.com/cwi-dis/iotsaNeoClock>.
This software is licensed under the [MIT license](LICENSE.txt) by the   CWI DIS group, <http://www.dis.cwi.nl>.

## Software requirements

* Arduino IDE, v1.6 or later.
* The iotsa framework, download from <https://github.com/cwi-dis/iotsa>.
* The new LiquidCrystal library, download from <https://bitbucket.org/fmalpartida/new-liquidcrystal>.
* The [Adafruit NeoPixel library](https://github.com/adafruit/Adafruit_NeoPixel).

## Hardware requirements

* a iotsa board (or another esp8266-based board).
* A strip of 60 NeoPixels.
* Optionally an LDR to auto-adjust the clock brightness.

## Hardware construction

Construction of the iotsa board is very simple: the NeoPixel strip _data in_ should be connected to _GPIO 14_ and _power_ and _ground_ should be connected to _5V_ and _GND_, respectively.

If you want light adaptation you also connect an LDR (light dependent resistor) between _AIN_ and _3V3_ and a resistor of around 10Kohm between _AIN_ and _GND_.

At this point you can test that things work (but you have to mentally group your 60 NeoPixels into 12 groups of 5, with the very first LED being at the top of the clock, and the last LED being the innermost at the 11 o'clock position).

Now you need to cut your NeoPixels into 12 groups of 5, and somehow affix them to a frame. There is a template in [extras/pixel-layout.pdf](extras/pixel-layout.pdf) to help you get things spaced correctly. The _data in_ of the 12 o'clock 5-pixel strip goes to the iotsa board. The _data out_ of the 12 o'clock 5 pixel strip goes to the _data in_ of the 1 o'clock 5 pixel strip. And so forth. The _data out_ of the 11 o'clock strip is not connected. All the _power_ are connected together (and to _5V_) and all the _ground_ are connected together (and to _GND_), how you do this does not matter. You probably want to tape or glue your wires to the template.

I bought a 5 euro clock with a glass front, ripped all the internals out and glued the NeoPixels to the template, then glued the template to what was left of the clock. Finally I put semi-opaque paper on the inside of the glass and put the clock together again.

If you have the LDR you want to somehow position it so that it catches the incident light on the clock (but not the light from the clock), and that it is not too ugly.

## Building the software

You need to define (or `#undef`) `WITH_BRIGHTNESS` depending on whether you want to be able to change the clock brightness over the net (and definitely if you have the LDR for auto-brightness).

If you want different colors for the hands your can adapt the `COLOR_SEC`, `COLOR_MIN` and `COLOR_HOUR` defines. If you want different patterns to show the time you need to modify the functions `neoClockShowSeconds()`, `neoClockShowMinutes()` and`neoClockShowHours()`.

## Operation

The first time the board boots it creates a Wifi network with a name similar to _config-iotsa1234_.  Connect a device to that network and visit <http://192.168.4.1>. Configure your device name (using the name _neoclock_ is suggested), WiFi name and password, and after reboot the iotsa board should connect to your network and be visible as <http://neoclock.local>.

### Configuration

Visit <http://neoclock.local/ntpconfig> to configure the NTP (Network Time Protocol) server to use (but the default _pool.ntp.org_ will usually work) and your timezone. After about half a minute the clock should have a fix and start displaying the right time. The time is also shown on the ntpconfig page and on the clock homepage.

You can change the brightness of the clock at <http://neoclock.local/brightness>. You can either set a single brightness value, or set _Adapt to ambient light_ and set a minimum and maximum brightness.

### Alerts

The clock can display alerts, which are sequences of patterns, there are sample patterns in [data](data).
Each line consists of 181 numbers: a duration (in milliseconds) and 60 R, G and B values for each of the pixels. Triggering an alert will simply show the lines in order.

Alerts are uploaded via <http://neoclock.local/upload> and then triggered by accessing <http://neoclock.local/alert?alert=inRed>, for example. Alerts can also be converted to a binary form (which may be slightly better for fast patterns), the script _extras/alert2bin.py_ can do the conversion.

Alerts are intended to be shown programmatically when something interesting has happened, such as a doorbell being pushed, or a facebook mention, or whatever you can think of. [Igor](https://github.com/cwi-dis/iotsa) is a good way of connecting triggers to alerts.

### Status indicators

The clock can also display _status information_, which is intended to be longer-lived. Think of things like "the garden door is open" or "the temperature in the freezer is above -20 C". There are two types of status indicators:

* the inner ring is used for _main status information_. All 12 LEDs show the same color and intensity.
* the outer ring is used for _temporal status information_, information for the coming hour. The LEDs all have the same color, but the intensity can vary. This can be used to get a quick visual indication of things like the expected rainfall in the coming hour.

Some example URLs to trigger status indicators:

```
http://neoclock.local/alert?status=0x00ff00
```

Sets the inner ring to bright green.

```
http://neoclock.local/alert?timeout=60&status=0x006600
```
Sets the inner ring to medium green for 60 seconds, then revert to normal.

```
http://neoclock.local/alert?timeout=300&status=/0xff0000
```
Sets the inner ring to normal (unlit) for 5 minutes, then go to a very bright red. This can be used as a watchdog: by re-issuing this command within those 5 minutes the red will never show up. Unless the agent that is responsible for contacting the URL is too late.

```
http://neoclock.local/alert?timeout=300&temporalStatus=0xccffff/1.0/0.0/0.0/0.5
```
Sets the outer ring with timed information. Where the minute had is pointing is set to 100% lightish blue, 15 minutes into the future to that color with 50% intensity. After 5 minutes the outer ring will revert to normal.
