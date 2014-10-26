/* 
  Copyright 2014 Mike McMahon

  LUFA Library
  Copyright 2014  Dean Camera (dean [at] fourwalledcubicle [dot] com)

  Permission to use, copy, modify, distribute, and sell this
  software and its documentation for any purpose is hereby granted
  without fee, provided that the above copyright notice appear in
  all copies and that both that the copyright notice and this
  permission notice and warranty disclaimer appear in supporting
  documentation, and that the name of the author not be used in
  advertising or publicity pertaining to distribution of the
  software without specific, written prior permission.

  The author disclaims all warranties with regard to this
  software, including all implied warranties of merchantability
  and fitness.  In no event shall the author be liable for any
  special, indirect or consequential damages or any damages
  whatsoever resulting from loss of use, data or profits, whether
  in an action of contract, negligence or other tortious action,
  arising out of or in connection with the use or performance of
  this software.
*/

/** \file
 *
 * Keyboard driver. Initializes hardware and converts scanned input to USB events.
 */

#include "Keyboard.h"

/** Buffer to hold the previously generated Keyboard HID report, for comparison purposes inside the HID class driver. */
static USB_KeyboardReport_Data_t PrevKeyboardReport;

/** LUFA HID Class driver interface configuration and state information. This structure is
 *  passed to all HID Class driver functions, so that multiple instances of the same class
 *  within a device can be differentiated from one another.
 */
USB_ClassInfo_HID_Device_t Keyboard_HID_Interface =
{
  .Config =
  {
    .InterfaceNumber        = INTERFACE_ID_Keyboard,
    .ReportINEndpoint       =
    {
      .Address              = KEYBOARD_EPADDR,
      .Size                 = KEYBOARD_EPSIZE,
      .Banks                = 1,
    },
    .PrevReportINBuffer     = &PrevKeyboardReport,
    .PrevReportINBufferSize = sizeof(PrevKeyboardReport),
  },
};

typedef enum {
  TK = 1, SPACE_CADET = 2, SMBX = 3
} Keyboard;

typedef enum {
  HUT1 = 1, EMACS
} TranslationMode;

typedef enum {
  NONE = 0, 
  L_SHIFT = 1, R_SHIFT, L_CONTROL, R_CONTROL, L_META, R_META, L_SUPER, R_SUPER,
  L_HYPER, R_HYPER, L_SYMBOL, R_SYMBOL, L_GREEK, R_GREEK,
  CAPS_LOCK, MODE_LOCK, ALT_LOCK, 
  REPEAT
} KeyShift;

#define SHIFT(s) (1L << s)

#define L_TOP L_SYMBOL
#define R_TOP R_SYMBOL

#define L_ALT L_META
#define R_ALT R_META
#define L_GUI L_SUPER
#define R_GUI R_SUPER

#define MAX_USB_SHIFT R_GUI

typedef uint8_t HidUsageID;

// Information about each key.
typedef struct {
  HidUsageID hidUsageID;        // Currently always from the Keyboard / Keypad page.
  KeyShift shift;
  PGM_P keysym;                 // Or NULL if an ordinary PC/AT-101 key with no symbol.
} KeyInfo;

// As much as possible, keysyms are taken from <gdk/gdkkeysyms.h>,
// which seems to be the most comprehensive list of X keysyms.

#define KEYSYM(name,keysym) static char name[] PROGMEM = keysym
#define NO_KEY(idx) { 0, NONE, NULL }
#define SHIFT_KEY(idx,hid,shift) { hid, shift, NULL }
#define PC_KEY(idx,hid,keysym) { hid, NONE, keysym }
// Currently the same, but might want a flag to say how standard the
// non-symbol usage is.
#define LISP_KEY(idx,hid,keysym) { hid, NONE, keysym }

typedef struct {
  union {
    unsigned all;
    struct {
      unsigned cxsent:1;
      unsigned atsent:1;
      unsigned hyper:1;
      unsigned super:1;
      unsigned meta:1;
      unsigned shift:1;
      unsigned control:1;
      unsigned keysym:1;
    };
  } f;
  PGM_P chars;
  uint8_t nchars;
} EmacsEvent;

static Keyboard CurrentKeyboard;
static TranslationMode CurrentMode;

static uint32_t CurrentShifts;
static HidUsageID KeysDown[16];
static uint8_t NKeysDown;

#define N_EMACS_EVENTS 8
static EmacsEvent EventBuffers[N_EMACS_EVENTS];
static uint8_t EmacsBufferIn, EmacsBufferOut;
static uint8_t EmacsBufferedCount;

static void KeyDown(const KeyInfo *key);
static void KeyUp(const KeyInfo *key);
static void CreateEmacsEvent(EmacsEvent *event, uint32_t shifts, PGM_P keysym);
static void AddEmacsReport(USB_KeyboardReport_Data_t* KeyboardReport);
static void AddKeyReport(USB_KeyboardReport_Data_t* KeyboardReport);

static void MIT_Init(void);
static void MIT_Read(void);
static void SMBX_Init(void);
static void SMBX_Scan(void);

#define LOW 0
#define HIGH 1

// Micro has RX & TX LEDs and user LED as LED3 (D13); other boards only have LED1.
#if LEDS_LED3 != 0
#define KEYDOWN_LED LEDS_LED3
#else
#define KEYDOWN_LED LEDS_LED1
#endif

#ifdef EXTERNAL_LEDS
#define XLEDS_PORT PORTF
#define XLEDS_DDR DDRF
#define XLEDS_NUMLOCK (1 << 7)
#define XLEDS_CAPSLOCK (1 << 5)
#define XLEDS_SCROLLLOCK (1 << 6)
#define XLEDS_OTHER (1 << 4)
#define XLEDS_ALL 0xF0
#endif

#ifdef LMKBD_SWITCH
#define SWITCH_PORT PORTF
#define SWITCH_DDR DDRF
#define SWITCH_PIN PINF
#define SWITCH_MASK 0x03
#endif

#define TK_DDR DDRD
#define TK_PIN PIND
#define TK_PORT PORTD
#define TK_KBDIN (1 << 0)
#define TK_KBDCLK (1 << 1)
#define SMBX_DDR DDRB
#define SMBX_PIN PINB
#define SMBX_PORT PORTB
#define SMBX_KBDIN (1 << 4)
#define SMBX_KBDNEXT (1 << 5)
#define SMBX_KBDSCAN (1 << 6)

static void LMKBD_Init(void)
{
  int i;

#ifdef EXTERNAL_LEDS
  XLEDS_DDR |= XLEDS_ALL;
  // Flash all LEDs on until we receive a host report with their proper state.
  XLEDS_PORT |= XLEDS_ALL;
#endif

#ifdef LMKBD_SWITCH
#ifdef LMKBD
#error LMKBD must not be defined if LMKBD_SWITCH is enabled in local.mk
#endif
  SWITCH_PORT |= SWITCH_MASK;   // Enable pullups.
  CurrentKeyboard = (Keyboard)(SWITCH_PIN & SWITCH_MASK);
#else
#ifndef LMKBD
#error LMKBD must be defined as keyboard type in local.mk
#endif
  CurrentKeyboard = LMKBD;
#endif

  CurrentMode = EMACS;

  CurrentShifts = 0;
  NKeysDown = 0;
  for (i = 0; i < sizeof(KeysDown); i++)
    KeysDown[i] = 0;

  EmacsBufferIn = EmacsBufferOut = 0;
  EmacsBufferedCount = 0;

  switch (CurrentKeyboard) {
  case TK:
  case SPACE_CADET:
    MIT_Init();
    break;
  case SMBX:  
    SMBX_Init();
    break;
  }
}

static void LMKBD_Task(void)
{
  switch (CurrentKeyboard) {
  case TK:
  case SPACE_CADET:
    if ((TK_PIN & TK_KBDIN) == LOW)
      MIT_Read();
    break;
  case SMBX:  
    SMBX_Scan();
    break;
  }

  if (NKeysDown > 0) {
      LEDs_TurnOnLEDs(KEYDOWN_LED);
  }
  else {
      LEDs_TurnOffLEDs(KEYDOWN_LED);
  }
}

static void KeyDown(/*PROGMEM*/ const KeyInfo *key)
{
  HidUsageID usage = pgm_read_byte(&key->hidUsageID);
  KeyShift shift = pgm_read_byte(&key->shift);
  PGM_P keysym = pgm_read_ptr(&key->keysym);
  
  switch (CurrentMode) {
  case EMACS:
    if (shift != NONE) {
      CurrentShifts |= SHIFT(shift);
      break;
    }
    if (keysym != NULL) {
      if (EmacsBufferedCount < N_EMACS_EVENTS) {
        EmacsEvent *event = &EventBuffers[EmacsBufferIn];
        CreateEmacsEvent(event, CurrentShifts, keysym);
        if (event->nchars > 0) {
          // Found actual keysym; queue for sending.
          EmacsBufferIn = (EmacsBufferIn + 1) % N_EMACS_EVENTS;
          EmacsBufferedCount++;
          break;
        }
      }
    }
    if (NKeysDown < sizeof(KeysDown)) {
      KeysDown[NKeysDown++] = usage;
    }
    if (CurrentShifts & (SHIFT(L_SUPER) | SHIFT(R_SUPER) | 
                         SHIFT(L_HYPER) | SHIFT(R_HYPER))) {
      // An ordinary keysym, but with unusual shifts.  Send prefix.
      // When we later catch up, the actual key will be sent.
      EmacsEvent *event = &EventBuffers[EmacsBufferIn];
      CreateEmacsEvent(event, 
                       (CurrentShifts & (SHIFT(L_SUPER) | SHIFT(R_SUPER) | 
                                         SHIFT(L_HYPER) | SHIFT(R_HYPER))),
                       NULL);
      EmacsBufferIn = (EmacsBufferIn + 1) % N_EMACS_EVENTS;
      EmacsBufferedCount++;
      break;
    }
    break;
    
  default:
    if (shift != NONE) {
      CurrentShifts |= SHIFT(shift);
      if (shift <= MAX_USB_SHIFT)
        break;                  // No need for usage entry.
    }
    if (NKeysDown < sizeof(KeysDown)) {
      KeysDown[NKeysDown++] = usage;
    }
    break;
  }
}

static void KeyUp(/*PROGMEM*/ const KeyInfo *key)
{
  HidUsageID usage = pgm_read_byte(&key->hidUsageID);
  KeyShift shift = pgm_read_byte(&key->shift);
  int i;

  if (shift != NONE) {
    CurrentShifts &= ~SHIFT(shift);
  }

  for (i = 0; i < NKeysDown; i++) {
    if (KeysDown[i] == usage) {
      NKeysDown--;
      while (i < NKeysDown) {
        KeysDown[i] = KeysDown[i+1];
        i++;
      }
      break;
    }
  }
}

static void CreateEmacsEvent(EmacsEvent *event, uint32_t shifts, PGM_P keysym)
{
  event->f.all = 0;
  if (shifts & (SHIFT(L_HYPER) | SHIFT(R_HYPER)))
    event->f.hyper = true;
  if (shifts & (SHIFT(L_SUPER) | SHIFT(R_SUPER)))
    event->f.super = true;
  if (shifts & (SHIFT(L_META) | SHIFT(R_META)))
    event->f.meta = true;
  if (shifts & (SHIFT(L_SHIFT) | SHIFT(R_SHIFT)))
    event->f.shift = true;
  if (shifts & (SHIFT(L_CONTROL) | SHIFT(R_CONTROL)))
    event->f.control = true;

  if (keysym == NULL) {
    event->chars = NULL;
    event->nchars = 0;
  }
  else {
    PGM_P chars;
    uint8_t nchars;
    int n;
    char ch;

     if (shifts & (SHIFT(L_GREEK) | SHIFT(R_GREEK)))
       n = 2;
     else if (shifts & (SHIFT(L_SYMBOL) | SHIFT(R_SYMBOL)))
       n = 1;
     else
       n = 0;
    
    chars = keysym;

    do {
      event->chars = chars;
      nchars = 0;
      while (true) {
        ch = pgm_read_byte(chars);
        if (ch == '\0')
          break;
        chars++;
        if (ch == ',')
          break;
        nchars++;
      }
      if (ch == '\0')
        break;
    } while (n-- > 0);

    event->nchars = nchars;

    event->f.keysym = true;
  }
}

static char ASCII2HUT1(char ch)
{
  // TODO: Could have a lookup table if this gets much more complicated.
  if ((ch >= 'a') && (ch <= 'z'))
    return HID_KEYBOARD_SC_A + (ch - 'a');
  if ((ch >= 'A') && (ch <= 'Z'))
    return HID_KEYBOARD_SC_A + (ch - 'A');
  if (ch == '0')
    return HID_KEYBOARD_SC_0_AND_CLOSING_PARENTHESIS;
  if ((ch >= '1') && (ch <= '9'))
    return HID_KEYBOARD_SC_1_AND_EXCLAMATION + (ch - '1');
  if ((ch == '-') || (ch == '_'))
    return HID_KEYBOARD_SC_MINUS_AND_UNDERSCORE;
  return 0;
}

static char IsKeyDown(char key)
{
  int i;
  if (!key) return false;
  for (i = 0; i < NKeysDown; i++) {
    if (KeysDown[i] == key)
      return true;
  }
  return false;
}

static void AddEmacsReport(USB_KeyboardReport_Data_t* KeyboardReport)
{
  EmacsEvent *event;
  HidUsageID key, prevKey;
  int i;

  event = &EventBuffers[EmacsBufferOut];
  
  // We try to avoid sending an extra report with no keys down between
  // characters.  However, when one is doubled, there is no alternative.
  // Therefore need to check key sent in last report.
  prevKey = PrevKeyboardReport.KeyCode[0];

  KeyboardReport->Modifier = 0;
  for (i = 1; i < sizeof(KeyboardReport->KeyCode); i++) {
    KeyboardReport->KeyCode[i] = 0;
  }
  
  if (event->f.all) {
    // Prefix stage.  Three substates: none, c-X sent, and c-X @ sent.
    if (!event->f.cxsent) {
      key = ASCII2HUT1('x');
      if (key == prevKey)
        KeyboardReport->KeyCode[0] = 0;
      else {
        KeyboardReport->Modifier = HID_KEYBOARD_MODIFIER_LEFTCTRL;
        KeyboardReport->KeyCode[0] = key;
        event->f.cxsent = true;
      }
    }
    else if (!event->f.atsent) {
      key = ASCII2HUT1('2');    // @
      if (key == prevKey)
        KeyboardReport->KeyCode[0] = 0;
      else {
        KeyboardReport->Modifier = HID_KEYBOARD_MODIFIER_LEFTSHIFT;
        KeyboardReport->KeyCode[0] = key;
        event->f.atsent = true;
      }
    }
    else if (event->f.hyper) {
      key = ASCII2HUT1('h');
      if (key == prevKey)
        KeyboardReport->KeyCode[0] = 0;
      else {
        KeyboardReport->KeyCode[0] = key;
        event->f.hyper = event->f.cxsent = event->f.atsent = false;
      }
    }
    else if (event->f.super) {
      key = ASCII2HUT1('s');
      if (key == prevKey)
        KeyboardReport->KeyCode[0] = 0;
      else {
        KeyboardReport->KeyCode[0] = key;
        event->f.super = event->f.cxsent = event->f.atsent = false;
      }
    }
    else if (event->f.meta) {
      key = ASCII2HUT1('m');
      if (key == prevKey)
        KeyboardReport->KeyCode[0] = 0;
      else {
        KeyboardReport->KeyCode[0] = key;
        event->f.meta = event->f.cxsent = event->f.atsent = false;
      }
    }
    else if (event->f.shift) {
      key = ASCII2HUT1('s');    // S
      if (key == prevKey)
        KeyboardReport->KeyCode[0] = 0;
      else {
        KeyboardReport->Modifier = HID_KEYBOARD_MODIFIER_LEFTSHIFT;
        KeyboardReport->KeyCode[0] = key;
        event->f.shift = event->f.cxsent = event->f.atsent = false;
      }
    }
    else if (event->f.control) {
      key = ASCII2HUT1('c');
      if (key == prevKey)
        KeyboardReport->KeyCode[0] = 0;
      else {
        KeyboardReport->KeyCode[0] = key;
        event->f.control = event->f.cxsent = event->f.atsent = false;
      }
    }
    else if (event->f.keysym) { 
      key = ASCII2HUT1('k');
      if (key == prevKey)
        KeyboardReport->KeyCode[0] = 0;
      else {
        KeyboardReport->KeyCode[0] = key;
        event->f.keysym = event->f.cxsent = event->f.atsent = false;
      }
    }
  }
  else {
    // Keysym stage.
    if (event->chars != NULL) {
      if (event->nchars > 0) {
        key = ASCII2HUT1(pgm_read_byte(event->chars));
        if (key == prevKey)
          KeyboardReport->KeyCode[0] = 0;
        else {
          KeyboardReport->KeyCode[0] = key;
          event->chars++;
          event->nchars--;
        }
      }
      else {
        key = HID_KEYBOARD_SC_ENTER;
        if (key == prevKey)
          KeyboardReport->KeyCode[0] = 0;
        else {
          KeyboardReport->KeyCode[0] = key;
          event->chars = NULL;
        }
      }
    }
    else {
      // There is nothing left to do for this event.
      if ((EmacsBufferedCount == 1) && IsKeyDown(prevKey))
        KeyboardReport->KeyCode[0] = 0;
      else {
        EmacsBufferOut = (EmacsBufferOut + 1) % N_EMACS_EVENTS;
        EmacsBufferedCount--;
        if (EmacsBufferedCount == 0)
          // Catch up with actual key settings.
          AddKeyReport(KeyboardReport);
      }
    }
  }
}

static void AddKeyReport(USB_KeyboardReport_Data_t* KeyboardReport)
{
  uint8_t shifts;
  int i;

#define ADD_SHIFT(m,s)          \
  if (CurrentShifts & SHIFT(s)) \
    shifts |= m;

  shifts = 0;
  ADD_SHIFT(HID_KEYBOARD_MODIFIER_LEFTCTRL,L_CONTROL);
  ADD_SHIFT(HID_KEYBOARD_MODIFIER_LEFTSHIFT,L_SHIFT);
  ADD_SHIFT(HID_KEYBOARD_MODIFIER_LEFTALT,L_ALT);
  ADD_SHIFT(HID_KEYBOARD_MODIFIER_LEFTGUI,L_GUI);
  ADD_SHIFT(HID_KEYBOARD_MODIFIER_RIGHTCTRL,R_CONTROL);
  ADD_SHIFT(HID_KEYBOARD_MODIFIER_RIGHTSHIFT,R_SHIFT);
  ADD_SHIFT(HID_KEYBOARD_MODIFIER_RIGHTALT,R_ALT);
  ADD_SHIFT(HID_KEYBOARD_MODIFIER_RIGHTGUI,R_GUI);
  KeyboardReport->Modifier = shifts;

  if (NKeysDown > sizeof(KeyboardReport->KeyCode)) {
    for (i = 0; i < sizeof(KeyboardReport->KeyCode); i++) {
      KeyboardReport->KeyCode[i] = HID_KEYBOARD_SC_ERROR_ROLLOVER;
    }
  }
  else {
    for (i = 0; i < sizeof(KeyboardReport->KeyCode); i++) {
      KeyboardReport->KeyCode[i] = (i < NKeysDown) ? KeysDown[i] : 0;
    }
  }
}

/*** Knight keyboards ***/

KEYSYM(KS_TK_00, "break");
KEYSYM(KS_TK_01, "escape");
KEYSYM(KS_TK_15, "atsign");
KEYSYM(KS_TK_16, "caret");
KEYSYM(KS_TK_20, "call");
KEYSYM(KS_TK_21, "clear");
KEYSYM(KS_TK_23, "altmode");
KEYSYM(KS_TK_44, "form");
KEYSYM(KS_TK_45, "vt");
KEYSYM(KS_TK_61, "colon");
KEYSYM(KS_TK_63, "line");
KEYSYM(KS_TK_64, "backnext");

static KeyInfo TKKeys[64] PROGMEM = {
  LISP_KEY(00, HID_KEYBOARD_SC_CANCEL, KS_TK_00), /* break */
  LISP_KEY(01, HID_KEYBOARD_SC_ESCAPE, KS_TK_01), /* esc */
  PC_KEY(02, HID_KEYBOARD_SC_1_AND_EXCLAMATION, NULL), /* 1 ! */
  PC_KEY(03, HID_KEYBOARD_SC_2_AND_AT, NULL), /* 2 " */
  PC_KEY(04, HID_KEYBOARD_SC_3_AND_HASHMARK, NULL), /* 3 # */
  PC_KEY(05, HID_KEYBOARD_SC_4_AND_DOLLAR, NULL), /* 4 $ */
  PC_KEY(06, HID_KEYBOARD_SC_5_AND_PERCENTAGE, NULL), /* 5 % */
  PC_KEY(07, HID_KEYBOARD_SC_6_AND_CARET, NULL), /* 6 & */
  PC_KEY(10, HID_KEYBOARD_SC_7_AND_AMPERSAND, NULL), /* 7 ' */
  PC_KEY(11, HID_KEYBOARD_SC_8_AND_ASTERISK, NULL), /* 8 ( */
  PC_KEY(12, HID_KEYBOARD_SC_9_AND_OPENING_PARENTHESIS, NULL), /* 9 ) */
  PC_KEY(13, HID_KEYBOARD_SC_0_AND_CLOSING_PARENTHESIS, NULL), /* 0 _ */
  PC_KEY(14, HID_KEYBOARD_SC_EQUAL_AND_PLUS, NULL), /* - = */
  LISP_KEY(15, HID_KEYBOARD_SC_KEYPAD_AT, KS_TK_15), /* @ ` */
  LISP_KEY(16, HID_KEYBOARD_SC_KEYPAD_CARET, KS_TK_16), /* ^ ~ */
  PC_KEY(17, HID_KEYBOARD_SC_INSERT, NULL), /* bs */
  LISP_KEY(20, HID_KEYBOARD_SC_OUT, KS_TK_20), /* call */
  LISP_KEY(21, HID_KEYBOARD_SC_CLEAR, KS_TK_21), /* clear */
  PC_KEY(22, HID_KEYBOARD_SC_TAB, NULL), /* tab */
  LISP_KEY(23, HID_KEYBOARD_SC_ESCAPE, KS_TK_23), /* alt */
  PC_KEY(24, HID_KEYBOARD_SC_Q, NULL), /* q conjunction */
  PC_KEY(25, HID_KEYBOARD_SC_W, NULL), /* w disjunction */
  PC_KEY(26, HID_KEYBOARD_SC_E, NULL), /* e uplump */
  PC_KEY(27, HID_KEYBOARD_SC_R, NULL), /* r downlump */
  PC_KEY(30, HID_KEYBOARD_SC_T, NULL), /* t leftlump */
  PC_KEY(31, HID_KEYBOARD_SC_Y, NULL), /* y rightlump */
  PC_KEY(32, HID_KEYBOARD_SC_U, NULL), /* u elbow */
  PC_KEY(33, HID_KEYBOARD_SC_I, NULL), /* i wheel */
  PC_KEY(34, HID_KEYBOARD_SC_O, NULL), /* o downarrow */
  PC_KEY(35, HID_KEYBOARD_SC_P, NULL), /* p uparrow */
  PC_KEY(36, HID_KEYBOARD_SC_OPENING_BRACKET_AND_OPENING_BRACE, NULL), /* [ { */
  PC_KEY(37, HID_KEYBOARD_SC_CLOSING_BRACKET_AND_CLOSING_BRACE, NULL), /* ] } */
  PC_KEY(40, HID_KEYBOARD_SC_BACKSLASH_AND_PIPE, NULL), /* \ | */
  PC_KEY(41, HID_KEYBOARD_SC_KEYPAD_SLASH, NULL), /* / infinity */
  PC_KEY(42, HID_KEYBOARD_SC_KEYPAD_MINUS, NULL), /* circle minus / delta */
  PC_KEY(43, HID_KEYBOARD_SC_KEYPAD_PLUS, NULL), /* circle plus / del */
  LISP_KEY(44, HID_KEYBOARD_SC_SEPARATOR, KS_TK_44), /* form */
  LISP_KEY(45, HID_KEYBOARD_SC_PAGE_DOWN, KS_TK_45), /* vt */
  PC_KEY(46, HID_KEYBOARD_SC_BACKSPACE, NULL), /* rubout */
  PC_KEY(47, HID_KEYBOARD_SC_A, NULL), /* a less or equal */
  PC_KEY(50, HID_KEYBOARD_SC_S, NULL), /* s greater or equal */
  PC_KEY(51, HID_KEYBOARD_SC_D, NULL), /* d equivalence */
  PC_KEY(52, HID_KEYBOARD_SC_F, NULL), /* f partial */
  PC_KEY(53, HID_KEYBOARD_SC_G, NULL), /* g not equal */
  PC_KEY(54, HID_KEYBOARD_SC_H, NULL), /* h help */
  PC_KEY(55, HID_KEYBOARD_SC_J, NULL), /* j leftarrow */
  PC_KEY(56, HID_KEYBOARD_SC_K, NULL), /* k rightarrow */
  PC_KEY(57, HID_KEYBOARD_SC_L, NULL), /* l botharrow */
  PC_KEY(60, HID_KEYBOARD_SC_SEMICOLON_AND_COLON, NULL), /* ; plus */
  LISP_KEY(61, HID_KEYBOARD_SC_KEYPAD_COLON, KS_TK_61), /* : * */
  PC_KEY(62, HID_KEYBOARD_SC_ENTER, NULL), /* return */
  LISP_KEY(63, HID_KEYBOARD_SC_KEYPAD_ENTER, KS_TK_63), /* line */
  LISP_KEY(64, HID_KEYBOARD_SC_PRIOR, KS_TK_64), /* backnext */
  PC_KEY(65, HID_KEYBOARD_SC_Z, NULL), /* z alpha */
  PC_KEY(66, HID_KEYBOARD_SC_X, NULL), /* x beta */
  PC_KEY(67, HID_KEYBOARD_SC_C, NULL), /* c epsilon */
  PC_KEY(70, HID_KEYBOARD_SC_V, NULL), /* v lambda */
  PC_KEY(71, HID_KEYBOARD_SC_B, NULL), /* b pi */
  PC_KEY(72, HID_KEYBOARD_SC_N, NULL), /* n universal */
  PC_KEY(73, HID_KEYBOARD_SC_M, NULL), /* m existential */
  PC_KEY(74, HID_KEYBOARD_SC_COMMA_AND_LESS_THAN_SIGN, NULL), /* , < */
  PC_KEY(75, HID_KEYBOARD_SC_DOT_AND_GREATER_THAN_SIGN, NULL), /* . > */
  PC_KEY(76, HID_KEYBOARD_SC_SLASH_AND_QUESTION_MARK, NULL), /* / ? */
  PC_KEY(77, HID_KEYBOARD_SC_SPACE, NULL) /* space */
};

static void TKShiftKeys(uint16_t mask)
{
#define UPDATE_SHIFTS(n,s)              \
  if (mask & ((uint16_t)1 << n))  \
    CurrentShifts |= SHIFT(s);          \
  else                                  \
    CurrentShifts &= ~SHIFT(s);

  UPDATE_SHIFTS(6,L_SHIFT);
  UPDATE_SHIFTS(7,R_SHIFT);
  UPDATE_SHIFTS(8,L_TOP);
  UPDATE_SHIFTS(9,R_TOP);
  UPDATE_SHIFTS(10,L_CONTROL);
  UPDATE_SHIFTS(11,R_CONTROL);
  UPDATE_SHIFTS(12,L_META);
  UPDATE_SHIFTS(13,R_META);
  UPDATE_SHIFTS(14,CAPS_LOCK);
}

/*** Space Cadet keyboards ***/

KEYSYM(KS_SC_001, "ii");
KEYSYM(KS_SC_002, "iv");
KEYSYM(KS_SC_011, ",,cent");
KEYSYM(KS_SC_012, ",union,rho");
KEYSYM(KS_SC_013, ",righttack,phi");
KEYSYM(KS_SC_014, ",similarequal,varsigma");
KEYSYM(KS_SC_017, "handright,,circleslash");
KEYSYM(KS_SC_021, "colon,plusminus,section");
KEYSYM(KS_SC_030, "holdoutput");
KEYSYM(KS_SC_031, ",,times");
KEYSYM(KS_SC_032, ",infinity,iota");
KEYSYM(KS_SC_033, ",rightarrow,kappa");
KEYSYM(KS_SC_034, ",,guillemotleft");
KEYSYM(KS_SC_036, "line");
KEYSYM(KS_SC_037, ",,doublevertbar");
KEYSYM(KS_SC_040, "terminal");
KEYSYM(KS_SC_042, "network");
KEYSYM(KS_SC_046, "status");
KEYSYM(KS_SC_047, "resume");
KEYSYM(KS_SC_050, "clearscreen");
KEYSYM(KS_SC_051, ",,quad");
KEYSYM(KS_SC_052, ",contained,psi");
KEYSYM(KS_SC_053, ",downarrow,eta");
KEYSYM(KS_SC_054, ",lessthanequal,nu");
KEYSYM(KS_SC_061, ",,doubledagger");
KEYSYM(KS_SC_062, ",logicalor,omega");
KEYSYM(KS_SC_063, ",uptack,sigma");
KEYSYM(KS_SC_064, ",ceiling,xi");
KEYSYM(KS_SC_067, "abort");
KEYSYM(KS_SC_071, ",,paragraph");
KEYSYM(KS_SC_072, ",exists,omicron");
KEYSYM(KS_SC_073, ",doublearrow,lambda");
KEYSYM(KS_SC_074, ",,guillemotright");
KEYSYM(KS_SC_077, ",,notsign");
KEYSYM(KS_SC_100, "macro");
KEYSYM(KS_SC_101, "i");
KEYSYM(KS_SC_102, "iii");
KEYSYM(KS_SC_106, "thumbup,,circleminus");
KEYSYM(KS_SC_107, "call");
KEYSYM(KS_SC_110, "clearinput");
KEYSYM(KS_SC_111, ",,degree");
KEYSYM(KS_SC_112, ",includes,tau");
KEYSYM(KS_SC_113, ",uparrow,gamma");
KEYSYM(KS_SC_114, ",identical,beta");
KEYSYM(KS_SC_116, "help");
KEYSYM(KS_SC_117, "handleft,,circletimes");
KEYSYM(KS_SC_120, "quote");
KEYSYM(KS_SC_121, ",,dagger");
KEYSYM(KS_SC_122, ",logicaland,theta");
KEYSYM(KS_SC_123, ",downtack,alpha");
KEYSYM(KS_SC_124, ",floor,zeta");
KEYSYM(KS_SC_126, ",,approximate");
KEYSYM(KS_SC_131, ",,horizbar");
KEYSYM(KS_SC_132, "parenleft,bracketleft,doublebracketleft");
KEYSYM(KS_SC_133, ",,periodcentered");
KEYSYM(KS_SC_137, "parenright,bracketright,doublebracketright");
KEYSYM(KS_SC_141, "system");
KEYSYM(KS_SC_143, "altmode");
KEYSYM(KS_SC_146, "braceright,rightanglebracket,broketright");
KEYSYM(KS_SC_151, ",,division");
KEYSYM(KS_SC_152, ",forall,upsilon");
KEYSYM(KS_SC_153, ",leftarrow,vartheta");
KEYSYM(KS_SC_154, ",greaterthanequal,mu");
KEYSYM(KS_SC_161, ",,del");
KEYSYM(KS_SC_162, ",intersection,epsilon");
KEYSYM(KS_SC_163, ",lefttack,delta");
KEYSYM(KS_SC_164, ",notequal,chi");
KEYSYM(KS_SC_166, "braceleft,leftanglebracket,broketleft");
KEYSYM(KS_SC_167, "break");
KEYSYM(KS_SC_170, "stopoutput");
KEYSYM(KS_SC_171, ",,circle");
KEYSYM(KS_SC_172, ",partialderivative,pi");
KEYSYM(KS_SC_173, ",,doubbaselinedot");
KEYSYM(KS_SC_174, ",,integral");
KEYSYM(KS_SC_176, "thumbdown,,circleplus");

static KeyInfo SpaceCadetKeys[128] PROGMEM = {
  NO_KEY(000),
  LISP_KEY(001, HID_KEYBOARD_SC_F10, KS_SC_001), /* II */
  LISP_KEY(002, HID_KEYBOARD_SC_F12, KS_SC_002), /* IV */
  SHIFT_KEY(003, HID_KEYBOARD_SC_LOCKING_SCROLL_LOCK, MODE_LOCK), /* mode lock */
  NO_KEY(004),
  SHIFT_KEY(005, HID_KEYBOARD_SC_LEFT_GUI, L_SUPER), /* left super */
  NO_KEY(006),
  NO_KEY(007),
  NO_KEY(010),
  PC_KEY(011, HID_KEYBOARD_SC_4_AND_DOLLAR, KS_SC_011), /* 4 */
  PC_KEY(012, HID_KEYBOARD_SC_R, KS_SC_012), /* r */
  PC_KEY(013, HID_KEYBOARD_SC_F, KS_SC_013), /* f */
  PC_KEY(014, HID_KEYBOARD_SC_V, KS_SC_014), /* v */
  SHIFT_KEY(015, HID_KEYBOARD_SC_LOCKING_NUM_LOCK, ALT_LOCK), /* alt lock */
  NO_KEY(016),
  LISP_KEY(017, HID_KEYBOARD_SC_RIGHT_ARROW, KS_SC_017), /* hand right */
  SHIFT_KEY(020, HID_KEYBOARD_SC_LEFT_CONTROL, L_CONTROL), /* left control */
  LISP_KEY(021, HID_KEYBOARD_SC_KEYPAD_COLON, KS_SC_021), /* plus minus */
  PC_KEY(022, HID_KEYBOARD_SC_TAB, NULL), /* tab */
  PC_KEY(023, HID_KEYBOARD_SC_BACKSPACE, NULL), /* rubout */
  SHIFT_KEY(024, HID_KEYBOARD_SC_LEFT_SHIFT, L_SHIFT), /* left shift */
  SHIFT_KEY(025, HID_KEYBOARD_SC_RIGHT_SHIFT, R_SHIFT), /* right shift */
  SHIFT_KEY(026, HID_KEYBOARD_SC_RIGHT_CONTROL, R_CONTROL), /* right control */
  NO_KEY(027),
  LISP_KEY(030, HID_KEYBOARD_SC_PAUSE, KS_SC_030), /* hold output */
  PC_KEY(031, HID_KEYBOARD_SC_8_AND_ASTERISK, KS_SC_031), /* 8 */
  PC_KEY(032, HID_KEYBOARD_SC_I, KS_SC_032), /* i */
  PC_KEY(033, HID_KEYBOARD_SC_K, KS_SC_033), /* k */
  PC_KEY(034, HID_KEYBOARD_SC_COMMA_AND_LESS_THAN_SIGN, KS_SC_034), /* comma */
  SHIFT_KEY(035, HID_KEYBOARD_SC_MEDIA_VOLUME_UP, R_GREEK), /* right greek */
  LISP_KEY(036, HID_KEYBOARD_SC_KEYPAD_ENTER, KS_SC_036), /* line */
  PC_KEY(037, HID_KEYBOARD_SC_BACKSLASH_AND_PIPE, KS_SC_037), /* back slash */
  LISP_KEY(040, HID_KEYBOARD_SC_OPER, KS_SC_040), /* terminal */
  NO_KEY(041),
  LISP_KEY(042, HID_KEYBOARD_SC_MENU, KS_SC_042), /* network */
  NO_KEY(043),
  SHIFT_KEY(044, HID_KEYBOARD_SC_MEDIA_EJECT, L_GREEK), /* left greek */
  SHIFT_KEY(045, HID_KEYBOARD_SC_LEFT_ALT, L_META), /* left meta */
  LISP_KEY(046, HID_KEYBOARD_SC_SYSREQ, KS_SC_046), /* status */
  LISP_KEY(047, HID_KEYBOARD_SC_RETURN, KS_SC_047), /* resume */
  LISP_KEY(050, HID_KEYBOARD_SC_CLEAR_AND_AGAIN, KS_SC_050), /* clear screen */
  PC_KEY(051, HID_KEYBOARD_SC_6_AND_CARET, KS_SC_051), /* 6 */
  PC_KEY(052, HID_KEYBOARD_SC_Y, KS_SC_052), /* y */
  PC_KEY(053, HID_KEYBOARD_SC_H, KS_SC_053), /* h */
  PC_KEY(054, HID_KEYBOARD_SC_N, KS_SC_054), /* n */
  NO_KEY(055),
  NO_KEY(056),
  NO_KEY(057),
  NO_KEY(060),
  PC_KEY(061, HID_KEYBOARD_SC_2_AND_AT, KS_SC_061), /* 2 */
  PC_KEY(062, HID_KEYBOARD_SC_W, KS_SC_062), /* w */
  PC_KEY(063, HID_KEYBOARD_SC_S, KS_SC_063), /* s */
  PC_KEY(064, HID_KEYBOARD_SC_X, KS_SC_064), /* x */
  SHIFT_KEY(065, HID_KEYBOARD_SC_RIGHT_GUI, R_SUPER), /* right super */
  NO_KEY(066),
  LISP_KEY(067, HID_KEYBOARD_SC_STOP, KS_SC_067), /* abort */
  NO_KEY(070),
  PC_KEY(071, HID_KEYBOARD_SC_9_AND_OPENING_PARENTHESIS, KS_SC_071), /* 9 */
  PC_KEY(072, HID_KEYBOARD_SC_O, KS_SC_072), /* o */
  PC_KEY(073, HID_KEYBOARD_SC_L, KS_SC_073), /* l */
  PC_KEY(074, HID_KEYBOARD_SC_DOT_AND_GREATER_THAN_SIGN, KS_SC_074), /* period */
  NO_KEY(075),
  NO_KEY(076),
  PC_KEY(077, HID_KEYBOARD_SC_GRAVE_ACCENT_AND_TILDE, KS_SC_077), /* back quote */
  LISP_KEY(100, HID_KEYBOARD_SC_AGAIN, KS_SC_100), /* macro */
  LISP_KEY(101, HID_KEYBOARD_SC_F9, KS_SC_101), /* I */
  LISP_KEY(102, HID_KEYBOARD_SC_F11, KS_SC_102), /* III */
  NO_KEY(103),
  SHIFT_KEY(104, HID_KEYBOARD_SC_MEDIA_PREVIOUS_TRACK, L_TOP), /* left top */
  NO_KEY(105),
  LISP_KEY(106, HID_KEYBOARD_SC_UP_ARROW, KS_SC_106), /* up thumb */
  LISP_KEY(107, HID_KEYBOARD_SC_OUT, KS_SC_107), /* call */
  LISP_KEY(110, HID_KEYBOARD_SC_CLEAR, KS_SC_110), /* clear input */
  PC_KEY(111, HID_KEYBOARD_SC_5_AND_PERCENTAGE, KS_SC_111), /* 5 */
  PC_KEY(112, HID_KEYBOARD_SC_T, KS_SC_112), /* t */
  PC_KEY(113, HID_KEYBOARD_SC_G, KS_SC_113), /* g */
  PC_KEY(114, HID_KEYBOARD_SC_B, KS_SC_114), /* b */
  SHIFT_KEY(115, HID_KEYBOARD_SC_MEDIA_VOLUME_DOWN, REPEAT), /* repeat */
  LISP_KEY(116, HID_KEYBOARD_SC_HELP, KS_SC_116), /* help */
  LISP_KEY(117, HID_KEYBOARD_SC_LEFT_ARROW, KS_SC_117), /* hand left */
  LISP_KEY(120, HID_KEYBOARD_SC_EXSEL, KS_SC_120), /* quote */
  PC_KEY(121, HID_KEYBOARD_SC_1_AND_EXCLAMATION, KS_SC_121), /* 1 */
  PC_KEY(122, HID_KEYBOARD_SC_Q, KS_SC_122), /* q */
  PC_KEY(123, HID_KEYBOARD_SC_A, KS_SC_123), /* a */
  PC_KEY(124, HID_KEYBOARD_SC_Z, KS_SC_124), /* z */
  SHIFT_KEY(125, HID_KEYBOARD_SC_LOCKING_CAPS_LOCK, CAPS_LOCK), /* caps lock */
  PC_KEY(126, HID_KEYBOARD_SC_EQUAL_AND_PLUS, KS_SC_126), /* equals */
  NO_KEY(127),
  NO_KEY(130),
  PC_KEY(131, HID_KEYBOARD_SC_MINUS_AND_UNDERSCORE, KS_SC_131), /* minus */
  LISP_KEY(132, HID_KEYBOARD_SC_KEYPAD_OPENING_PARENTHESIS, KS_SC_132), /* ( */
  PC_KEY(133, HID_KEYBOARD_SC_APOSTROPHE_AND_QUOTE, KS_SC_133), /* apostrophe */
  PC_KEY(134, HID_KEYBOARD_SC_SPACE, NULL), /* space */
  NO_KEY(135),
  PC_KEY(136, HID_KEYBOARD_SC_ENTER, NULL), /* return */
  LISP_KEY(137, HID_KEYBOARD_SC_KEYPAD_CLOSING_PARENTHESIS, KS_SC_137), /* ) */
  NO_KEY(140),
  LISP_KEY(141, HID_KEYBOARD_SC_APPLICATION, KS_SC_141), /* system */
  NO_KEY(142),
  LISP_KEY(143, HID_KEYBOARD_SC_ESCAPE, KS_SC_143), /* alt mode */
  NO_KEY(144),
  SHIFT_KEY(145, HID_KEYBOARD_SC_MEDIA_PLAY, L_HYPER), /* left hyper */
  LISP_KEY(146, HID_KEYBOARD_SC_KEYPAD_CLOSING_BRACE, KS_SC_146), /* } */
  NO_KEY(147),
  NO_KEY(150),
  PC_KEY(151, HID_KEYBOARD_SC_7_AND_AMPERSAND, KS_SC_151), /* 7 */
  PC_KEY(152, HID_KEYBOARD_SC_U, KS_SC_152), /* u */
  PC_KEY(153, HID_KEYBOARD_SC_J, KS_SC_153), /* j */
  PC_KEY(154, HID_KEYBOARD_SC_M, KS_SC_154), /* m */
  SHIFT_KEY(155, HID_KEYBOARD_SC_MEDIA_NEXT_TRACK, R_TOP), /* right top */
  PC_KEY(156, HID_KEYBOARD_SC_END, NULL), /* end */
  PC_KEY(157, HID_KEYBOARD_SC_DELETE, NULL), /* delete */
  PC_KEY(160, HID_KEYBOARD_SC_INSERT, NULL), /* overstrike */
  PC_KEY(161, HID_KEYBOARD_SC_3_AND_HASHMARK, KS_SC_161), /* 3 */
  PC_KEY(162, HID_KEYBOARD_SC_E, KS_SC_162), /* e */
  PC_KEY(163, HID_KEYBOARD_SC_D, KS_SC_163), /* d */
  PC_KEY(164, HID_KEYBOARD_SC_C, KS_SC_164), /* c */
  SHIFT_KEY(165, HID_KEYBOARD_SC_RIGHT_ALT, R_META), /* right meta */
  LISP_KEY(166, HID_KEYBOARD_SC_KEYPAD_OPENING_BRACE, KS_SC_166), /* { */
  LISP_KEY(167, HID_KEYBOARD_SC_CANCEL, KS_SC_167), /* break */
  LISP_KEY(170, HID_KEYBOARD_SC_PAUSE, KS_SC_170), /* stop output */
  PC_KEY(171, HID_KEYBOARD_SC_0_AND_CLOSING_PARENTHESIS, KS_SC_171), /* 0 */
  PC_KEY(172, HID_KEYBOARD_SC_P, KS_SC_172), /* p */
  PC_KEY(173, HID_KEYBOARD_SC_SEMICOLON_AND_COLON, KS_SC_173), /* semicolon */
  PC_KEY(174, HID_KEYBOARD_SC_SLASH_AND_QUESTION_MARK, KS_SC_174), /* question */
  SHIFT_KEY(175, HID_KEYBOARD_SC_MEDIA_STOP, R_HYPER), /* right hyper */
  LISP_KEY(176, HID_KEYBOARD_SC_DOWN_ARROW, KS_SC_176), /* down thumb */
  NO_KEY(177)
};

static void SpaceCadetAllKeysUp(uint16_t mask)
{
  HidUsageID key;
  uint32_t shift;
  int i, j;

  if (0 == mask) {
    CurrentShifts = 0;
    NKeysDown = 0;
  }
  else {
#define UPDATE_SHIFTS_LR(n,s)                                   \
    if (mask & ((uint16_t)1 << n)) {                            \
      if ((CurrentShifts & (SHIFT(L_##s) | SHIFT(R_##s))) == 0) \
        CurrentShifts |= SHIFT(L_##s);                          \
    }                                                           \
    else                                                        \
      CurrentShifts &= ~(SHIFT(L_##s) | SHIFT(R_##s));

    UPDATE_SHIFTS_LR(0,SHIFT);
    UPDATE_SHIFTS_LR(1,GREEK);
    UPDATE_SHIFTS_LR(2,TOP);
    UPDATE_SHIFTS(3,CAPS_LOCK);
    UPDATE_SHIFTS_LR(4,CONTROL);
    UPDATE_SHIFTS_LR(5,META);
    UPDATE_SHIFTS_LR(6,SUPER);
    UPDATE_SHIFTS_LR(7,HYPER);
    UPDATE_SHIFTS(8,ALT_LOCK);
    UPDATE_SHIFTS(9,MODE_LOCK);
    UPDATE_SHIFTS(10,REPEAT);

    j = 0;
    for (i = 0; i < NKeysDown; i++) {
      key = KeysDown[i];
      // Check for shift keys that are sent as ordinary usage ids
      // instead of in the prefix.
      switch (key) {
#define MAP_SHIFT(u,s)          \
      case u:                   \
        shift = SHIFT(s);       \
        break;

      MAP_SHIFT(HID_KEYBOARD_SC_LOCKING_CAPS_LOCK,CAPS_LOCK);
      MAP_SHIFT(HID_KEYBOARD_SC_LOCKING_NUM_LOCK,ALT_LOCK);
      MAP_SHIFT(HID_KEYBOARD_SC_LOCKING_SCROLL_LOCK,MODE_LOCK);
      MAP_SHIFT(HID_KEYBOARD_SC_MEDIA_PLAY,L_HYPER);
      MAP_SHIFT(HID_KEYBOARD_SC_MEDIA_STOP,R_HYPER);
      MAP_SHIFT(HID_KEYBOARD_SC_MEDIA_PREVIOUS_TRACK,L_SYMBOL);
      MAP_SHIFT(HID_KEYBOARD_SC_MEDIA_NEXT_TRACK,R_SYMBOL);
      MAP_SHIFT(HID_KEYBOARD_SC_MEDIA_EJECT,L_GREEK);
      MAP_SHIFT(HID_KEYBOARD_SC_MEDIA_VOLUME_UP,R_GREEK);
      MAP_SHIFT(HID_KEYBOARD_SC_MEDIA_VOLUME_DOWN,REPEAT);

      default:
        shift = 0;
      }
      if (CurrentShifts & shift) {
        // A still active shifting key is preserved.
        if (j != i)
          KeysDown[j] = key;
        j++;
      }
    }
    NKeysDown = j;
  }
}

static uint8_t tkBits[3];

void MIT_Init(void)
{
  TK_DDR |= TK_KBDCLK;
  TK_PORT |= TK_KBDCLK;         // Clock idle until data goes low.
}

/** Read and process 24 bits of code.
 * See MOON;KBD PROTOC for interpretation.
 */
static void MIT_Read(void)
{
  int i,j;

  for (i = 0; i < 3; i++) {
    uint8_t code = 0;
    for (j = 0; j < 8; j++) {
      TK_PORT &= ~TK_KBDCLK;    // Clock low.
      _delay_us(20);            // 50kHz.
      if ((TK_PIN & TK_KBDIN) == HIGH) {
        code |= (1 << j);
      }
      TK_PORT |= TK_KBDCLK;     // Clock high (idle).
      _delay_us(20);            // 50kHz.
    }
    tkBits[i] = code;
  }
  switch (tkBits[2]) {
  case 0xF9:
    switch (tkBits[1] & 0xC0) {
    case 0:
      if (tkBits[1] & 0x01)
        KeyUp(&SpaceCadetKeys[tkBits[0]]);
      else
        KeyDown(&SpaceCadetKeys[tkBits[0]]);
      break;
    case 0x80:
      SpaceCadetAllKeysUp(tkBits[0] | (((uint16_t)tkBits[1] & 0x07) << 8));
      break;
    }
    break;
  case 0xFF:
    TKShiftKeys((tkBits[0] & 0xC0) | ((uint16_t)tkBits[1] << 8));
    NKeysDown = 0;              // There are no up transitions.
    KeyDown(&TKKeys[tkBits[0] & 0x3F]);
    break;
  }
}

/*** Symbolics keyboards ***/

KEYSYM(KS_SM_002, "local");
KEYSYM(KS_SM_010, "scroll");
KEYSYM(KS_SM_015, "select");
KEYSYM(KS_SM_037, "abort");
KEYSYM(KS_SM_052, "help");
KEYSYM(KS_SM_065, "complete");
KEYSYM(KS_SM_071, "network");
KEYSYM(KS_SM_100, "line");
KEYSYM(KS_SM_104, "function");
KEYSYM(KS_SM_112, "parenright");
KEYSYM(KS_SM_113, "page");
KEYSYM(KS_SM_125, "parenleft");
KEYSYM(KS_SM_132, "colon");
KEYSYM(KS_SM_154, "vertbar");
KEYSYM(KS_SM_160, "escape");
KEYSYM(KS_SM_161, "refresh");
KEYSYM(KS_SM_162, "square");
KEYSYM(KS_SM_163, "circle");
KEYSYM(KS_SM_164, "triangle");
KEYSYM(KS_SM_165, "clearinput");
KEYSYM(KS_SM_166, "suspend");
KEYSYM(KS_SM_167, "resume");

static KeyInfo SMBXKeys[128] PROGMEM = {
  NO_KEY(000),
  NO_KEY(001),
  LISP_KEY(002, HID_KEYBOARD_SC_OPER, KS_SM_002), /* local */
  SHIFT_KEY(003, HID_KEYBOARD_SC_LOCKING_CAPS_LOCK, CAPS_LOCK), /* caps lock */
  SHIFT_KEY(004, HID_KEYBOARD_SC_MEDIA_PLAY, L_HYPER), /* left hyper */
  SHIFT_KEY(005, HID_KEYBOARD_SC_LEFT_ALT, L_META), /* left meta */
  SHIFT_KEY(006, HID_KEYBOARD_SC_RIGHT_CONTROL, R_CONTROL), /* right control */
  SHIFT_KEY(007, HID_KEYBOARD_SC_RIGHT_GUI, R_SUPER), /* right super */
  LISP_KEY(010, HID_KEYBOARD_SC_PAGE_DOWN, KS_SM_010), /* scroll */
  SHIFT_KEY(011, HID_KEYBOARD_SC_LOCKING_SCROLL_LOCK, MODE_LOCK), /* mode lock */
  NO_KEY(012),
  NO_KEY(013),
  NO_KEY(014),
  LISP_KEY(015, HID_KEYBOARD_SC_APPLICATION, KS_SM_015), /* select */
  SHIFT_KEY(016, HID_KEYBOARD_SC_MEDIA_PREVIOUS_TRACK, L_SYMBOL), /* left symbol */
  SHIFT_KEY(017, HID_KEYBOARD_SC_LEFT_GUI, L_SUPER), /* left super */
  SHIFT_KEY(020, HID_KEYBOARD_SC_LEFT_CONTROL, L_CONTROL), /* left control */
  PC_KEY(021, HID_KEYBOARD_SC_SPACE, NULL), /* space */
  SHIFT_KEY(022, HID_KEYBOARD_SC_RIGHT_ALT, R_META), /* right meta */
  SHIFT_KEY(023, HID_KEYBOARD_SC_MEDIA_STOP, R_HYPER), /* right hyper */
  PC_KEY(024, HID_KEYBOARD_SC_END, NULL), /* end */
  NO_KEY(025),
  NO_KEY(026),
  NO_KEY(027),
  PC_KEY(030, HID_KEYBOARD_SC_Z, NULL), /* z */
  PC_KEY(031, HID_KEYBOARD_SC_C, NULL), /* c */
  PC_KEY(032, HID_KEYBOARD_SC_B, NULL), /* b */
  PC_KEY(033, HID_KEYBOARD_SC_M, NULL), /* m */
  PC_KEY(034, HID_KEYBOARD_SC_DOT_AND_GREATER_THAN_SIGN, NULL), /* . */
  SHIFT_KEY(035, HID_KEYBOARD_SC_RIGHT_SHIFT, R_SHIFT), /* right shift */
  SHIFT_KEY(036, HID_KEYBOARD_SC_MEDIA_VOLUME_DOWN, REPEAT), /* repeat */
  LISP_KEY(037, HID_KEYBOARD_SC_STOP, KS_SM_037), /* abort */
  NO_KEY(040),
  NO_KEY(041),
  NO_KEY(042),
  SHIFT_KEY(043, HID_KEYBOARD_SC_LEFT_SHIFT, L_SHIFT), /* left shift */
  PC_KEY(044, HID_KEYBOARD_SC_X, NULL), /* x */
  PC_KEY(045, HID_KEYBOARD_SC_V, NULL), /* v */
  PC_KEY(046, HID_KEYBOARD_SC_N, NULL), /* n */
  PC_KEY(047, HID_KEYBOARD_SC_COMMA_AND_LESS_THAN_SIGN, NULL), /* , */
  PC_KEY(050, HID_KEYBOARD_SC_SLASH_AND_QUESTION_MARK, NULL), /* / */
  SHIFT_KEY(051, HID_KEYBOARD_SC_MEDIA_NEXT_TRACK, R_SYMBOL), /* right symbol */
  LISP_KEY(052, HID_KEYBOARD_SC_HELP, KS_SM_052), /* help */
  NO_KEY(053),
  NO_KEY(054),
  NO_KEY(055),
  PC_KEY(056, HID_KEYBOARD_SC_BACKSPACE, NULL), /* rubout */
  PC_KEY(057, HID_KEYBOARD_SC_S, NULL), /* s */
  PC_KEY(060, HID_KEYBOARD_SC_F, NULL), /* f */
  PC_KEY(061, HID_KEYBOARD_SC_H, NULL), /* h */
  PC_KEY(062, HID_KEYBOARD_SC_K, NULL), /* k */
  PC_KEY(063, HID_KEYBOARD_SC_SEMICOLON_AND_COLON, NULL), /* ; */
  PC_KEY(064, HID_KEYBOARD_SC_ENTER, NULL), /* return */
  LISP_KEY(065, HID_KEYBOARD_SC_EXSEL, KS_SM_065), /* complete */
  NO_KEY(066),
  NO_KEY(067),
  NO_KEY(070),
  LISP_KEY(071, HID_KEYBOARD_SC_MENU, KS_SM_071), /* network */
  PC_KEY(072, HID_KEYBOARD_SC_A, NULL), /* a */
  PC_KEY(073, HID_KEYBOARD_SC_D, NULL), /* d */
  PC_KEY(074, HID_KEYBOARD_SC_G, NULL), /* g */
  PC_KEY(075, HID_KEYBOARD_SC_J, NULL), /* j */
  PC_KEY(076, HID_KEYBOARD_SC_L, NULL), /* l */
  PC_KEY(077, HID_KEYBOARD_SC_APOSTROPHE_AND_QUOTE, NULL), /* ' */
  LISP_KEY(100, HID_KEYBOARD_SC_KEYPAD_ENTER, KS_SM_100), /* line */
  NO_KEY(101),
  NO_KEY(102),
  NO_KEY(103),
  LISP_KEY(104, HID_KEYBOARD_SC_AGAIN, KS_SM_104), /* function */
  PC_KEY(105, HID_KEYBOARD_SC_W, NULL), /* w */
  PC_KEY(106, HID_KEYBOARD_SC_R, NULL), /* r */
  PC_KEY(107, HID_KEYBOARD_SC_Y, NULL), /* y */
  PC_KEY(110, HID_KEYBOARD_SC_I, NULL), /* i */
  PC_KEY(111, HID_KEYBOARD_SC_P, NULL), /* p */
  LISP_KEY(112, HID_KEYBOARD_SC_KEYPAD_CLOSING_PARENTHESIS, KS_SM_112), /* ) */
  LISP_KEY(113, HID_KEYBOARD_SC_SEPARATOR, KS_SM_113), /* page */
  NO_KEY(114),
  NO_KEY(115),
  NO_KEY(116),
  PC_KEY(117, HID_KEYBOARD_SC_TAB, NULL), /* tab */
  PC_KEY(120, HID_KEYBOARD_SC_Q, NULL), /* q */
  PC_KEY(121, HID_KEYBOARD_SC_E, NULL), /* e */
  PC_KEY(122, HID_KEYBOARD_SC_T, NULL), /* t */
  PC_KEY(123, HID_KEYBOARD_SC_U, NULL), /* u */
  PC_KEY(124, HID_KEYBOARD_SC_O, NULL), /* o */
  LISP_KEY(125, HID_KEYBOARD_SC_KEYPAD_OPENING_PARENTHESIS, KS_SM_125), /* ( */
  PC_KEY(126, HID_KEYBOARD_SC_INSERT, NULL), /* backspace */
  NO_KEY(127),
  NO_KEY(130),
  NO_KEY(131),
  LISP_KEY(132, HID_KEYBOARD_SC_KEYPAD_COLON, KS_SM_132), /* : */
  PC_KEY(133, HID_KEYBOARD_SC_2_AND_AT, NULL), /* 2 */
  PC_KEY(134, HID_KEYBOARD_SC_4_AND_DOLLAR, NULL), /* 4 */
  PC_KEY(135, HID_KEYBOARD_SC_6_AND_CARET, NULL), /* 6 */
  PC_KEY(136, HID_KEYBOARD_SC_8_AND_ASTERISK, NULL), /* 8 */
  PC_KEY(137, HID_KEYBOARD_SC_0_AND_CLOSING_PARENTHESIS, NULL), /* 0 */
  PC_KEY(140, HID_KEYBOARD_SC_EQUAL_AND_PLUS, NULL), /* = */
  PC_KEY(141, HID_KEYBOARD_SC_BACKSLASH_AND_PIPE, NULL), /* \ */
  NO_KEY(142),
  NO_KEY(143),
  NO_KEY(144),
  PC_KEY(145, HID_KEYBOARD_SC_1_AND_EXCLAMATION, NULL), /* 1 */
  PC_KEY(146, HID_KEYBOARD_SC_3_AND_HASHMARK, NULL), /* 3 */
  PC_KEY(147, HID_KEYBOARD_SC_5_AND_PERCENTAGE, NULL), /* 5 */
  PC_KEY(150, HID_KEYBOARD_SC_7_AND_AMPERSAND, NULL), /* 7 */
  PC_KEY(151, HID_KEYBOARD_SC_9_AND_OPENING_PARENTHESIS, NULL), /* 9 */
  PC_KEY(152, HID_KEYBOARD_SC_MINUS_AND_UNDERSCORE, NULL), /* - */
  PC_KEY(153, HID_KEYBOARD_SC_GRAVE_ACCENT_AND_TILDE, NULL), /* ` */
  LISP_KEY(154, HID_KEYBOARD_SC_KEYPAD_PIPE, KS_SM_154), /* | */
  NO_KEY(155),
  NO_KEY(156),
  NO_KEY(157),
  LISP_KEY(160, HID_KEYBOARD_SC_ESCAPE, KS_SM_160), /* escape */
  LISP_KEY(161, HID_KEYBOARD_SC_CLEAR_AND_AGAIN, KS_SM_161), /* refresh */
  LISP_KEY(162, HID_KEYBOARD_SC_F9, KS_SM_162), /* square */
  LISP_KEY(163, HID_KEYBOARD_SC_F10, KS_SM_163), /* circle */
  LISP_KEY(164, HID_KEYBOARD_SC_F11, KS_SM_164), /* triangle */
  LISP_KEY(165, HID_KEYBOARD_SC_CLEAR, KS_SM_165), /* clear input */
  LISP_KEY(166, HID_KEYBOARD_SC_CANCEL, KS_SM_166), /* suspend */
  LISP_KEY(167, HID_KEYBOARD_SC_RETURN, KS_SM_167), /* resume */
  NO_KEY(170),
  NO_KEY(171),
  NO_KEY(172),
  NO_KEY(173),
  NO_KEY(174),
  NO_KEY(175),
  NO_KEY(176),
  NO_KEY(177)
};

static uint8_t smbxKeyStates[16], smbxNKeyStates[16];

static void SMBX_Init(void)
{
  int i;

  SMBX_DDR |= (SMBX_KBDSCAN | SMBX_KBDNEXT);
  SMBX_PORT |= (SMBX_KBDSCAN | SMBX_KBDNEXT);

  for (i = 0; i < 16; i++)
    smbxKeyStates[i] = 0;
}

static void SMBX_Scan(void)
{
  int i,j;

  SMBX_PORT &= ~SMBX_KBDSCAN;
  SMBX_PORT |= SMBX_KBDSCAN;
  for (i = 0; i < 16; i++) {
    uint8_t code = 0;
    for (j = 0; j < 8; j++) {
      SMBX_PORT &= ~SMBX_KBDNEXT;
      SMBX_PORT |= SMBX_KBDNEXT;
      if ((SMBX_PIN & SMBX_KBDIN) == LOW) {
        code |= (1 << j);
      }
    }
    smbxNKeyStates[i] = code;
  }

  for (i = 0; i < 16; i++) {
    uint8_t keys, change;
    keys = smbxNKeyStates[i];
    change = keys ^ smbxKeyStates[i];
    if (change == 0) continue;
    smbxKeyStates[i] = keys;
    for (j = 0; j < 8; j++) {
      if (change & (1 << j)) {
        int code = (i * 8) + j;
        if (keys & (1 << j)) {
          KeyDown(&SMBXKeys[code]);
        }
        else {
          KeyUp(&SMBXKeys[code]);
        }
      }
    }    
  }
}

/*** TI Keyboards ***/

KEYSYM(KS_TI_004, "boldlock");
KEYSYM(KS_TI_005, "itallock");
KEYSYM(KS_TI_010, "system");
KEYSYM(KS_TI_011, "network");
KEYSYM(KS_TI_012, "status");
KEYSYM(KS_TI_013, "terminal");
KEYSYM(KS_TI_015, "clearscreen");
KEYSYM(KS_TI_016, "clearinput");
KEYSYM(KS_TI_017, "undo");
KEYSYM(KS_TI_021, "left");
KEYSYM(KS_TI_022, "middle");
KEYSYM(KS_TI_023, "right");
KEYSYM(KS_TI_041, "resume");
KEYSYM(KS_TI_043, "escape");
KEYSYM(KS_TI_066, "break");
KEYSYM(KS_TI_103, "parenleft,bracketleft");
KEYSYM(KS_TI_104, "parenright,bracketleft");
KEYSYM(KS_TI_114, "abort");
KEYSYM(KS_TI_134, "line");

static KeyInfo ExplorerKeys[128] PROGMEM = {
  NO_KEY(000),
  LISP_KEY(001, HID_KEYBOARD_SC_HELP, NULL), // HELP
  NO_KEY(002),
  SHIFT_KEY(003, HID_KEYBOARD_SC_CAPS_LOCK, CAPS_LOCK), // CAPS-LOCK
  LISP_KEY(004, HID_KEYBOARD_SC_MEDIA_VOLUME_DOWN, KS_TI_004), // BOLD-LOCK (shift key? LED?)
  LISP_KEY(005, HID_KEYBOARD_SC_MEDIA_MUTE, KS_TI_005), // ITAL-LOCK (shift key? LED?)
  SHIFT_KEY(006, HID_KEYBOARD_SC_SCROLL_LOCK, MODE_LOCK), // MODE-LOCK
  SHIFT_KEY(007, HID_KEYBOARD_SC_MEDIA_PLAY, L_HYPER), // LEFT-HYPER
  LISP_KEY(010, HID_KEYBOARD_SC_APPLICATION, KS_TI_010), // SYSTEM
  LISP_KEY(011, HID_KEYBOARD_SC_MENU, KS_TI_011), // NETWORK
  LISP_KEY(012, HID_KEYBOARD_SC_SYSREQ, KS_TI_012), // STATUS
  LISP_KEY(013, HID_KEYBOARD_SC_OPER, KS_TI_013), // TERMINAL
  NO_KEY(014),
  LISP_KEY(015, HID_KEYBOARD_SC_CLEAR_AND_AGAIN, KS_TI_015), // CLEAR-SCREEN
  LISP_KEY(016, HID_KEYBOARD_SC_CLEAR, KS_TI_016), // CLEAR-INPUT
  LISP_KEY(017, HID_KEYBOARD_SC_UNDO, KS_TI_017), // UNDO
  PC_KEY(020, HID_KEYBOARD_SC_END, NULL), // END
  LISP_KEY(021, HID_KEYBOARD_SC_F9, KS_TI_021), // LEFT (mouse keys? like i ii iii?)
  LISP_KEY(022, HID_KEYBOARD_SC_F10, KS_TI_022), // MIDDLE
  LISP_KEY(023, HID_KEYBOARD_SC_F11, KS_TI_023), // RIGHT
  PC_KEY(024, HID_KEYBOARD_SC_F1, NULL), // F1
  PC_KEY(025, HID_KEYBOARD_SC_F2, NULL), // F2
  PC_KEY(026, HID_KEYBOARD_SC_F3, NULL), // F3
  PC_KEY(027, HID_KEYBOARD_SC_F4, NULL), // F4
  NO_KEY(030),
  NO_KEY(031),
  SHIFT_KEY(032, HID_KEYBOARD_SC_LEFT_GUI, L_SUPER), // LEFT-SUPER
  SHIFT_KEY(033, HID_KEYBOARD_SC_LEFT_ALT, L_META), // LEFT-META
  SHIFT_KEY(034, HID_KEYBOARD_SC_LEFT_CONTROL, L_CONTROL), // LEFT-CONTROL
  SHIFT_KEY(035, HID_KEYBOARD_SC_RIGHT_CONTROL, R_CONTROL), // RIGHT-CONTROL
  SHIFT_KEY(036, HID_KEYBOARD_SC_RIGHT_ALT, R_META), // RIGHT-META
  SHIFT_KEY(037, HID_KEYBOARD_SC_RIGHT_GUI, R_SUPER), // RIGHT-SUPER
  SHIFT_KEY(040, HID_KEYBOARD_SC_MEDIA_STOP, R_HYPER), // RIGHT-HYPER
  LISP_KEY(041, HID_KEYBOARD_SC_RETURN, KS_TI_041), // RESUME
  NO_KEY(042),
  LISP_KEY(043, HID_KEYBOARD_SC_ESCAPE, KS_TI_043), // ALT (ESCAPE actually?)
  PC_KEY(044, HID_KEYBOARD_SC_1_AND_EXCLAMATION, NULL), // 1
  PC_KEY(045, HID_KEYBOARD_SC_2_AND_AT, NULL), // 2
  PC_KEY(046, HID_KEYBOARD_SC_3_AND_HASHMARK, NULL), // 3
  PC_KEY(047, HID_KEYBOARD_SC_4_AND_DOLLAR, NULL), // 4
  PC_KEY(050, HID_KEYBOARD_SC_5_AND_PERCENTAGE, NULL), // 5
  PC_KEY(051, HID_KEYBOARD_SC_6_AND_CARET, NULL), // 6
  PC_KEY(052, HID_KEYBOARD_SC_7_AND_AMPERSAND, NULL), // 7
  PC_KEY(053, HID_KEYBOARD_SC_8_AND_ASTERISK, NULL), // 8
  PC_KEY(054, HID_KEYBOARD_SC_9_AND_OPENING_PARENTHESIS, NULL), // 9
  PC_KEY(055, HID_KEYBOARD_SC_0_AND_CLOSING_PARENTHESIS, NULL), // 0
  PC_KEY(056, HID_KEYBOARD_SC_MINUS_AND_UNDERSCORE, NULL), // MINUS
  PC_KEY(057, HID_KEYBOARD_SC_EQUAL_AND_PLUS, NULL), // EQUALS
  PC_KEY(060, HID_KEYBOARD_SC_KEYPAD_OPENING_BRACE, NULL), // BACK-QUOTE (` {)
  PC_KEY(061, HID_KEYBOARD_SC_KEYPAD_CLOSING_BRACE, NULL), // TILDE (~ })
  PC_KEY(062, HID_KEYBOARD_SC_KEYPAD_EQUAL_SIGN, NULL), // KEYPAD-EQUAL
  PC_KEY(063, HID_KEYBOARD_SC_KEYPAD_PLUS, NULL), // KEYPAD-PLUS
  PC_KEY(064, HID_KEYBOARD_SC_KEYPAD_SPACE, NULL), // KEYPAD-SPACE
  PC_KEY(065, HID_KEYBOARD_SC_KEYPAD_TAB, NULL), // KEYPAD-TAB
  LISP_KEY(066, HID_KEYBOARD_SC_CANCEL, KS_TI_066), // BREAK
  NO_KEY(067),
  PC_KEY(070, HID_KEYBOARD_SC_TAB, NULL), // TAB
  PC_KEY(071, HID_KEYBOARD_SC_Q, NULL), // Q
  PC_KEY(072, HID_KEYBOARD_SC_W, NULL), // W
  PC_KEY(073, HID_KEYBOARD_SC_E, NULL), // E
  PC_KEY(074, HID_KEYBOARD_SC_R, NULL), // R
  PC_KEY(075, HID_KEYBOARD_SC_T, NULL), // T
  PC_KEY(076, HID_KEYBOARD_SC_Y, NULL), // Y
  PC_KEY(077, HID_KEYBOARD_SC_U, NULL), // U
  PC_KEY(100, HID_KEYBOARD_SC_I, NULL), // I
  PC_KEY(101, HID_KEYBOARD_SC_O, NULL), // O
  PC_KEY(102, HID_KEYBOARD_SC_P, NULL), // P
  LISP_KEY(103, HID_KEYBOARD_SC_KEYPAD_OPENING_PARENTHESIS, KS_TI_103), // OPEN-PARENTHESIS
  LISP_KEY(104, HID_KEYBOARD_SC_KEYPAD_CLOSING_PARENTHESIS, KS_TI_104), // CLOSE-PARENTHESIS
  NO_KEY(105),
  PC_KEY(106, HID_KEYBOARD_SC_BACKSLASH_AND_PIPE, NULL), // BACKSLASH
  PC_KEY(107, HID_KEYBOARD_SC_UP_ARROW, NULL), // UP-ARROW
  PC_KEY(110, HID_KEYBOARD_SC_KEYPAD_7_AND_HOME, NULL), // KEYPAD-7
  PC_KEY(111, HID_KEYBOARD_SC_KEYPAD_8_AND_UP_ARROW, NULL), // KEYPAD-8
  PC_KEY(112, HID_KEYBOARD_SC_KEYPAD_9_AND_PAGE_UP, NULL), // KEYPAD-9
  PC_KEY(113, HID_KEYBOARD_SC_KEYPAD_MINUS, NULL), // KEYPAD-MINUS
  LISP_KEY(114, HID_KEYBOARD_SC_STOP, KS_TI_114), // ABORT
  NO_KEY(115),
  NO_KEY(116),
  PC_KEY(117, HID_KEYBOARD_SC_BACKSPACE, NULL), // RUBOUT
  PC_KEY(120, HID_KEYBOARD_SC_A, NULL), // A
  PC_KEY(121, HID_KEYBOARD_SC_S, NULL), // S
  PC_KEY(122, HID_KEYBOARD_SC_D, NULL), // D
  PC_KEY(123, HID_KEYBOARD_SC_F, NULL), // F
  PC_KEY(124, HID_KEYBOARD_SC_G, NULL), // G
  PC_KEY(125, HID_KEYBOARD_SC_H, NULL), // H
  PC_KEY(126, HID_KEYBOARD_SC_J, NULL), // J
  PC_KEY(127, HID_KEYBOARD_SC_K, NULL), // K
  PC_KEY(130, HID_KEYBOARD_SC_L, NULL), // L
  PC_KEY(131, HID_KEYBOARD_SC_SEMICOLON_AND_COLON, NULL), // SEMICOLON
  PC_KEY(132, HID_KEYBOARD_SC_APOSTROPHE_AND_QUOTE, NULL), // APOSTROPHE
  PC_KEY(133, HID_KEYBOARD_SC_ENTER, NULL), // RETURN
  LISP_KEY(134, 0xA5, KS_TI_134),  // LINE (others use HID_KEYBOARD_SC_KEYPAD_ENTER)
  PC_KEY(135, HID_KEYBOARD_SC_LEFT_ARROW, NULL), // LEFT-ARROW
  PC_KEY(136, HID_KEYBOARD_SC_HOME, NULL), // HOME
  PC_KEY(137, HID_KEYBOARD_SC_RIGHT_ARROW, NULL), // RIGHT-ARROW
  PC_KEY(140, HID_KEYBOARD_SC_KEYPAD_4_AND_LEFT_ARROW, NULL), // KEYPAD-4
  PC_KEY(141, HID_KEYBOARD_SC_KEYPAD_5, NULL), // KEYPAD-5
  PC_KEY(142, HID_KEYBOARD_SC_KEYPAD_6_AND_RIGHT_ARROW, NULL), // KEYPAD-6
  PC_KEY(143, HID_KEYBOARD_SC_KEYPAD_COMMA, NULL), // KEYPAD-COMMA
  NO_KEY(144),
  NO_KEY(145),
  SHIFT_KEY(146, HID_KEYBOARD_SC_MEDIA_PREVIOUS_TRACK, L_SYMBOL), // LEFT-SYMBOL
  SHIFT_KEY(147, HID_KEYBOARD_SC_LEFT_SHIFT, L_SHIFT), // LEFT-SHIFT
  PC_KEY(150, HID_KEYBOARD_SC_Z, NULL), // Z
  PC_KEY(151, HID_KEYBOARD_SC_X, NULL), // X
  PC_KEY(152, HID_KEYBOARD_SC_C, NULL), // C
  PC_KEY(153, HID_KEYBOARD_SC_V, NULL), // V
  PC_KEY(154, HID_KEYBOARD_SC_B, NULL), // B
  PC_KEY(155, HID_KEYBOARD_SC_N, NULL), // N
  PC_KEY(156, HID_KEYBOARD_SC_M, NULL), // M
  PC_KEY(157, HID_KEYBOARD_SC_COMMA_AND_LESS_THAN_SIGN, NULL), // COMMA
  PC_KEY(160, HID_KEYBOARD_SC_DOT_AND_GREATER_THAN_SIGN, NULL), // PERIOD
  PC_KEY(161, HID_KEYBOARD_SC_SLASH_AND_QUESTION_MARK, NULL), // QUESTION
  SHIFT_KEY(162, HID_KEYBOARD_SC_RIGHT_SHIFT, R_SHIFT), // RIGHT-SHIFT
  NO_KEY(163),
  SHIFT_KEY(164, HID_KEYBOARD_SC_MEDIA_NEXT_TRACK, R_SYMBOL), // RIGHT-SYMBOL
  PC_KEY(165, HID_KEYBOARD_SC_DOWN_ARROW, NULL), // DOWN-ARROW
  PC_KEY(166, HID_KEYBOARD_SC_KEYPAD_1_AND_END, NULL), // KEYPAD-1
  PC_KEY(167, HID_KEYBOARD_SC_KEYPAD_2_AND_DOWN_ARROW, NULL), // KEYPAD-2
  PC_KEY(170, HID_KEYBOARD_SC_KEYPAD_3_AND_PAGE_DOWN, NULL), // KEYPAD-3
  NO_KEY(171),
  NO_KEY(172),
  PC_KEY(173, HID_KEYBOARD_SC_SPACE, NULL), // SPACE
  NO_KEY(174),
  PC_KEY(175, HID_KEYBOARD_SC_KEYPAD_0_AND_INSERT, NULL), // KEYPAD-0
  PC_KEY(176, HID_KEYBOARD_SC_KEYPAD_DOT_AND_DELETE, NULL), // KEYPAD-PERIOD
  PC_KEY(177, HID_KEYBOARD_SC_KEYPAD_ENTER, NULL) // KEYPAD-ENTER
};

/*** Device Application ***/

/** Main program entry point. This routine contains the overall program flow, including initial
 *  setup of all components and the main program loop.
 */
int main(void)
{
  SetupHardware();

  LEDs_SetAllLEDs(LEDMASK_USB_NOTREADY);
  GlobalInterruptEnable();

  while (true) {
    LMKBD_Task();
    HID_Device_USBTask(&Keyboard_HID_Interface);
    USB_USBTask();
  }
}

/** Configures the board hardware and keyboard pins. */
void SetupHardware(void)
{
#if (ARCH == ARCH_AVR8)
  /* Disable watchdog if enabled by bootloader/fuses */
  MCUSR &= ~(1 << WDRF);
  wdt_disable();

  /* Disable clock division */
  clock_prescale_set(clock_div_1);
#elif (ARCH == ARCH_XMEGA)
  /* Start the PLL to multiply the 2MHz RC oscillator to 32MHz and switch the CPU core to run from it */
  XMEGACLK_StartPLL(CLOCK_SRC_INT_RC2MHZ, 2000000, F_CPU);
  XMEGACLK_SetCPUClockSource(CLOCK_SRC_PLL);

  /* Start the 32MHz internal RC oscillator and start the DFLL to increase it to 48MHz using the USB SOF as a reference */
  XMEGACLK_StartInternalOscillator(CLOCK_SRC_INT_RC32MHZ);
  XMEGACLK_StartDFLL(CLOCK_SRC_INT_RC32MHZ, DFLL_REF_INT_USBSOF, F_USB);

  PMIC.CTRL = PMIC_LOLVLEN_bm | PMIC_MEDLVLEN_bm | PMIC_HILVLEN_bm;
#endif

  /* Hardware Initialization */
  LMKBD_Init();
  LEDs_Init();
  USB_Init();
}

/** Event handler for the library USB Connection event. */
void EVENT_USB_Device_Connect(void)
{
  LEDs_SetAllLEDs(LEDMASK_USB_ENUMERATING);
}

/** Event handler for the library USB Disconnection event. */
void EVENT_USB_Device_Disconnect(void)
{
  LEDs_SetAllLEDs(LEDMASK_USB_NOTREADY);
}

/** Event handler for the library USB Configuration Changed event. */
void EVENT_USB_Device_ConfigurationChanged(void)
{
  bool ConfigSuccess = true;

  ConfigSuccess &= HID_Device_ConfigureEndpoints(&Keyboard_HID_Interface);

  USB_Device_EnableSOFEvents();

  LEDs_SetAllLEDs(ConfigSuccess ? LEDMASK_USB_READY : LEDMASK_USB_ERROR);
}

/** Event handler for the library USB Control Request reception event. */
void EVENT_USB_Device_ControlRequest(void)
{
  HID_Device_ProcessControlRequest(&Keyboard_HID_Interface);
}

/** Event handler for the USB device Start Of Frame event. */
void EVENT_USB_Device_StartOfFrame(void)
{
  HID_Device_MillisecondElapsed(&Keyboard_HID_Interface);
}

/** HID class driver callback function for the creation of HID reports to the host.
 *
 *  \param[in]     HIDInterfaceInfo  Pointer to the HID class interface configuration structure being referenced
 *  \param[in,out] ReportID    Report ID requested by the host if non-zero, otherwise callback should set to the generated report ID
 *  \param[in]     ReportType  Type of the report to create, either HID_REPORT_ITEM_In or HID_REPORT_ITEM_Feature
 *  \param[out]    ReportData  Pointer to a buffer where the created report should be stored
 *  \param[out]    ReportSize  Number of bytes written in the report (or zero if no report is to be sent)
 *
 *  \return Boolean \c true to force the sending of the report, \c false to let the library determine if it needs to be sent
 */
bool CALLBACK_HID_Device_CreateHIDReport(USB_ClassInfo_HID_Device_t* const HIDInterfaceInfo,
                                         uint8_t* const ReportID,
                                         const uint8_t ReportType,
                                         void* ReportData,
                                         uint16_t* const ReportSize)
{
  switch (ReportType) {
  case HID_REPORT_ITEM_In:
    {
      USB_KeyboardReport_Data_t* KeyboardReport = (USB_KeyboardReport_Data_t*)ReportData;
      switch (CurrentMode) {
      case EMACS:
        if (EmacsBufferedCount > 0) {
          AddEmacsReport(KeyboardReport);
          break;
        }
        /* else falls through */
      default:
        AddKeyReport(KeyboardReport);
        break;
      }
      *ReportSize = sizeof(USB_KeyboardReport_Data_t);
    }
    return false;
  case HID_REPORT_ITEM_Feature:
    {
      uint8_t* FeatureReport = (uint8_t*)ReportData;
      FeatureReport[0] = (uint8_t)CurrentKeyboard;
      FeatureReport[1] = (uint8_t)CurrentMode;
      *ReportSize = 2;
    }
    return true;
  default:
    *ReportSize = 0;
    return false;
  }
}

/** HID class driver callback function for the processing of HID reports from the host.
 *
 *  \param[in] HIDInterfaceInfo  Pointer to the HID class interface configuration structure being referenced
 *  \param[in] ReportID    Report ID of the received report from the host
 *  \param[in] ReportType  The type of report that the host has sent, either HID_REPORT_ITEM_Out or HID_REPORT_ITEM_Feature
 *  \param[in] ReportData  Pointer to a buffer where the received report has been stored
 *  \param[in] ReportSize  Size in bytes of the received HID report
 */
void CALLBACK_HID_Device_ProcessHIDReport(USB_ClassInfo_HID_Device_t* const HIDInterfaceInfo,
                                          const uint8_t ReportID,
                                          const uint8_t ReportType,
                                          const void* ReportData,
                                          const uint16_t ReportSize)
{
  switch (ReportType) {
  case HID_REPORT_ITEM_Out:
    if (ReportSize > 0) {
#ifdef EXTERNAL_LEDS
      uint8_t* LEDReport = (uint8_t*)ReportData;
      uint8_t  LEDMask   = 0;

      if (*LEDReport & HID_KEYBOARD_LED_NUMLOCK)
        LEDMask |= XLEDS_NUMLOCK;
      if (*LEDReport & HID_KEYBOARD_LED_CAPSLOCK)
        LEDMask |= XLEDS_CAPSLOCK;
      if (*LEDReport & HID_KEYBOARD_LED_SCROLLLOCK)
        LEDMask |= XLEDS_SCROLLLOCK;
      if (*LEDReport & (HID_KEYBOARD_LED_COMPOSE|HID_KEYBOARD_LED_KANA))
        LEDMask |= XLEDS_OTHER;

      XLEDS_PORT |= (XLEDS_PORT & ~XLEDS_ALL) | LEDMask;
#endif
      }
      break;
  case HID_REPORT_ITEM_Feature:
    if (ReportSize > 1) {
      uint8_t* FeatureReport = (uint8_t*)ReportData;
      CurrentMode = (TranslationMode)FeatureReport[1];
    }
    break;
  }
}
