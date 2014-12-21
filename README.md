# Lisp Machine Keyboard USB driver #

An AVR update of a PIC-based [driver](https://github.com/MMcM/lmkbd).

All the heavy lifting is done by the
[LUFA](http://www.fourwalledcubicle.com/LUFA.php) library. The driver is designed for
an ATmega32U4, but probably works with anything similar that LUFA supports.

## Supported Keyboards ##

* Symbolics keyboard with 6-pin RJ12 modular cable.
* MIT / Symbolics / LMI Space Cadet keyboard with 34-pin IDC ribbon cable.
* MIT Knight keyboard with Amphenol 9-pin mini-hex cable.

The code includes key codes for the TI Explorer, but I do not have information on the
encoding protocol or access to an example. Feel free to submit an issue if you do and
want it supported.

Note that the Knight keyboard does not send key up transitions, so
chording and auto-repeat will not work.

## Hardware ##

There are a number of readily available boards which have all the required components
except the keyboard connector itself.

* Arduino [Leonardo](http://arduino.cc/en/Main/arduinoBoardLeonardo) / [Micro](http://arduino.cc/en/Main/arduinoBoardMicro).
* [Sparkfun Pro Micro](https://www.sparkfun.com/products/12640).
* [Adafruit Atmega32u4 breakout](http://www.ladyada.net/products/atmega32u4breakout/).
* [Teensy 2.0](https://www.pjrc.com/teensy/index.html).
* [A-Star 32U4 Micro](http://www.pololu.com/product/3101).

Boards with an ICSP can be programmed through that or the using the AVR109 protocol
CDC class bootloader that the Arduino IDE uses.

### Connections ###

| Signal       | Pin | Arduino | RJ12 color | IDC | Mini-hex |
|--------------|-----|---------|------------|-----|----------|
| GND          | GND | GND     | blue       |  34 | B,D,F,J  |
| +5V          | VCC | 5V      | green      |  20 | H        |
| SMBX_KBDIN   | PB4 | D8      | red        |     |          |
| SMBX_KBDNEXT | PB5 | D9      | black      |     |          |
| SMBX_KBDSCAN | PB6 | D10     | white      |     |          |
| TK_KBDIN     | PD0 | D3      |            |   3 | C        |
| TK_KBDCLK    | PD1 | D2      |            |   2 | A        |

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

### Emacs Support ###

By default, when the Mode Lock key is locked, the keyboard sends
escape sequences for non-standard shifts, named control characters,
and every one of the legends on the Space Cadet keyboard. These
sequences can be decoded by extended versions of the `c-X @` prefix
characters in the `function-key-map` of modern Emacs 24 (or 25 beta)
or XEmacs 21.4. The graphic legends are translated to Unicode
codepoints and defined as self-inserting. The control keys are
translated to symbolic keysyms.

Some obvious aliases are predefined, such as `line` to `(control ?j)`
and `scroll` to `(control ?v)`.

## Windows Note ##

By default, Mode Lock is also translated into the HID locking Scroll
Lock key.  This seems to confuse Windows. Since Scroll Lock probably
does not do anything useful, setting
`-DMODE_LOCK_MODE=MODE_LOCK_MODE_2_SILENT` will prevent telling the
host that the key is down at all. If you do want Scroll Lock, but do
not want Emacs mode, `-DMODE_LOCK_MODE=MODE_LOCK_NONE` does that.

## APL Characters ##

Space Cadet keyboards are famous for having both the complete Greek
alphabet and the complete APL character set. A few legends need to
serve both, but have separate Unicode codepoints. For these,
Front/Greek will get the Greek one and Top+Front will get the APL one.

| Key | Greek    | APL      |
|-----|----------|----------|
| a   | &#x03B1; | &#x237A; |
| d   | &#x2206; | &#x2206; |
| e   | &#x03B5; | &#x2208; |
| i   | &#x03B9; | &#x2373; |
| w   | &#x03C9; | &#x2375; |
| r   | &#x03C1; | &#x2374; |

## Brokets ##

The front legend on the two Space Cadet brace keys have broken
brackets ("brokets").  There are no Unicode characters for
these. Instead, Front gives the bottom corner and Top+Front gives the
top corner of such a shape.

| Key | Front    | Top+Front |
|-----|----------|-----------|
| {   | &#x231E; | &#x231C;  |
| }   | &#x231F; | &#x231D;  |

## Customizing Symbol ##

Symbolics keyboards do not have symbol legends, although there is a
Symbol key. This is translated into Emacs's Alt- prefix (which is not
the same as the Meta- prefix you get from the Alt key on a PC
keyboard). This can be mapped to one of the Space Cadet keysyms, or to
a Unicode character code.

```el
(define-key function-key-map [(alt ?a)] [alpha])
(define-key function-key-map [(alt shift ?a)] [(shift alpha)])
(define-key function-key-map [(alt ?1)] [#x2603])
```

### XKB Support ###

Special escape sequences work with Emacs on different operating
systems, including Windows and OS X. (XEmacs requires a MULE version.)
But only while in Emacs. On systems that use the X Keyboard Extension,
such as Linux, the keyboard mapping can be configured to send Unicode
for the graphic characters, the Hyper- prefix and unique keysyms for
all the function keys. These then work with any application.

Key usage codes have been restricted to ones that the HID kernel
driver maps to evdev events. (Note that there are a few cases, such as
Cancel and Keypad-colon, where there is an appropriate HID code and a
corresponding evdev event, but the actual mapping is missing. These,
too, have been avoided, though a kernel device driver to do the
additional mappings would probably be trivial.)

The symbol and geometry files should be copied to the corresponding
system directories, which are someplace like `/usr/share/X11/xkb`. And
the evdev rules file should be patched to select these for the
supported keyboard models, `tk`, `space_cadet` and `smbx`.

```bash
setxkbmap -model space_cadet
```
