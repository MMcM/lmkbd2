# Lisp Machine Keyboard USB driver #

An AVR update of a PIC-based [driver](https://github.com/MMcM/lmkbd).

All the heavy lifting is done by the
[LUFA](http://www.fourwalledcubicle.com/LUFA.php) library. The driver is designed for
an ATmega32U4, but probably works with anything similar that LUFA supports.

## Supported Keyboards ##

* Symbolics keyboard with 6-pin RJ12 modular cable.
* MIT / Symbolics / LMI Space Cadet keyboard with 34-pin IDC ribbon cable.
* MIT Knight keyboard with Amphenol 9-pin mini-hex cable (probably).

The code includes key codes for the TI Explorer, but I do not have information on the
encoding protocol or access to an example. Feel free to submit an issue if you do and
want it supported.

## Hardware ##

There are a number of readily available boards which have all the required components
except the keyboard connector itself.

* Arduino [Leonardo](http://arduino.cc/en/Main/arduinoBoardLeonardo) / [Micro](http://arduino.cc/en/Main/arduinoBoardMicro).
* [Sparkfun Pro Micro](https://www.sparkfun.com/products/12640).
* [Adafruit Atmega32u4 breakout](http://www.ladyada.net/products/atmega32u4breakout/).
* [Teensy 2.0](https://www.pjrc.com/teensy/index.html).

Boards with an ICSP can be programmed through that or the using the AVR109 protocol
CDC class bootloader that the Arduino IDE uses.

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

An two-pole switch can be wired between PF&lt;0:1&gt; (Arduino D23/A5 &amp;
D22/A4) and GND to allow hardware selection of the keyboard type.

An LED array can be wired to PF&lt;4:7&gt; (Arduino D21-D18/A3-A0) to
display standard keyboard LEDs. This is less useful for these
keyboards, because all the locking shift keys are physically locking.

## Space Cadet Direct ##

The weak link for working Space Cadet keyboards seems to be the 8748. The
EPROM charge lasts a decade or more, but these keyboards are more than
thirty years old. NOS replacements are available on eBay, but don't seem
entirely reliable.

It is possible to have the ATmega scan the demux directly, though fifteen
connections are needed. I used an Adafruit breakout, because of the nice
physical placement of adjacent signals. I soldered the headers on the front
instead of the back and used M-F jumpers inserted into the chip socket, so
that everything is reversible.

| 8748 signal(s) | 8748 pin(s) | ATmega pin(s) | Signal       |
|----------------|-------------|---------------|--------------|
| Vss            | 20          | GND           | GND          |
| Vcc            | 40          | 5V            | +5V          |
| P1&lt;0&gt;    | 27          | PB0           | demux strobe |
| P1&lt;1:4&gt;  | 28-31       | PB4-7         | demux addr   |
| P2&lt;0:3&gt;  | 21-24       | PD0-3         | key mask     |
| P2&lt;4:7&gt;  | 35-38       | PD4-7         |              |
