# Lisp Machine Keyboard USB driver #

An AVR update of a PIC-based [driver](https://github.com/MMcM/lmkbd).

All the heavy lifting is done by the
[LUFA](http://www.fourwalledcubicle.com/LUFA.php) library. The driver is designed for
an ATmega32U4, but probably works with anything similar that LUFA supports.

## Hardware ##

There are a number of readily available boards which have all the required components
except the keyboard connector itself, an RJ12 jack for Symbolics keyboards and a
34-connector IDC header for the Space Cadet.

* [Arduino Micro](http://arduino.cc/en/Main/arduinoBoardMicro).
* [Sparkfun Pro Micro](https://www.sparkfun.com/products/12640).
* [Adafruit Atmega32u4 breakout](http://www.ladyada.net/products/atmega32u4breakout/).
* [Teensy 2.0](https://www.pjrc.com/teensy/index.html).

### Connections ###

| Signal       | Pin | Arduino | RJ12 color |
|--------------|-----|---------|------------|
| GND          | GND | GND     | blue       |
| +5V          | VCC | 5V      | green      |
| SMBX_KBDIN   | PB4 | D8      | red        |
| SMBX_KBDNEXT | PB5 | D9      | black      |
| SMBX_KBDSCAN | PB6 | D10     | white      |
| TK_KBDIN     | PD0 | D3      |            |
| TK_KBDCLK    | PD1 | D2      |            |
