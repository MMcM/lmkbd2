;;; -*- Mode: Emacs-Lisp; coding: utf-8 -*-

(if (string-match "XEmacs" emacs-version)

;;; XEmacs version
(progn
(require 'un-define)
;(set-coding-priority-list '(utf-8))
;(set-coding-category-system 'utf-8 'utf-8)

(defun event-recursive-function-key-map (ignore-prompt)
  "Return the next key event, and apply `function-key-map' as long as there is a binding.
Useful when a keysym entered via `synthesize-keysym' itself has an entry, possibly with shift."
  ;; First half lifted from event-apply-modifier.
  (let (events binding)
    ;; read keystrokes scanning `function-key-map'
    (while (keymapp
            (setq binding
                  (lookup-key
                   function-key-map
                   (vconcat
                    (setq events
                          (append events (list (next-key-event)))))))))
    (if binding                         ; found a binding
        (progn
          ;; allow for several modifiers
          (if (and (symbolp binding) (fboundp binding))
              (setq binding (funcall binding nil)))
          (setq events (append binding nil))
          ;; put remaining keystrokes back into input queue
          (setq unread-command-events
                (mapcar 'character-to-event (cdr events))))
      (setq unread-command-events (cdr events)))
    (let ((event (aref (key-sequence-list-description (car events)) 0))
          base-event modifiers shifts)
      (while (setq base-event (car (last event))
                   modifiers (butlast event)
                   shifts (intersection modifiers '(shift alt))
                   base-event (if shifts (append shifts (list base-event)) base-event)
                   modifiers (set-difference modifiers shifts)
                   binding (lookup-key function-key-map (vector base-event)))
        (if (and (symbolp binding) (fboundp binding))
            (setq binding (funcall binding nil)))
        (setq events (append binding nil)
              unread-command-events (append (mapcar 'character-to-event (cdr events))
                                            unread-command-events)
              base-event (aref (key-sequence-list-description (car events)) 0)
              event (append modifiers base-event)))
      (vector event))))

(define-key function-key-map [?\C-x ?@ ?q] 'event-recursive-function-key-map)

(defun lmkbd-graphic-unicode (key shifts code)
  "Register Unicode graphic character for possible shifted key.
Key can be either a key symbol or a character."
  (if (< code #x0080)
      (define-key function-key-map
                  (if shifts (vector (append shifts (list key))) key)
                  (vector (int-char code)))
    (let ((char (ucs-to-char code)))
      (if (null char)
          (display-warning 'lmkbd
            (format "Unicode U+%04X (%s) not mapped." code key))
        (if shifts
            (let ((name (if (symbolp key)
                            (symbol-name key)
                          (format "u_%04X" (if (numberp key) key (char-to-ucs key)))))
                  shifted-keysym)
              (if (memq 'shift shifts)
                  (setq name (capitalize name)))
              (if (memq 'alt shifts)
                  (setq name (concat name "_")))
              (setq shifted-keysym (intern name))
              (define-key function-key-map
                          (vector (append shifts (list key)))
                          (vector shifted-keysym))
              (setq key shifted-keysym)))
        (if (symbolp key)
            (set-character-of-keysym key char))
        (global-set-key key 'self-insert-command)
        (if (eq system-type 'linux)
            ;; Also handle when not in Emacs mode.
            (let ((evdev-code-key (intern (format "U%04X" code))))
              (set-character-of-keysym evdev-code-key char)
              (global-set-key evdev-code-key 'self-insert-command)))))))

)

;;; GNU Emacs version
(progn

;;; Make C-X @ handle nesting and C-X @ k like XEmacs.

(defun read-event-for-modifier ()
  "Read an event like `read-event', but applying `function-key-map' to allow multiple modifiers"
  (let (events binding)
    (while (keymapp
            (setq binding
                  (lookup-key
                   function-key-map
                   (vconcat (setq events (append events (list (read-event)))))))))
    (if binding
        (progn
          (if (and (symbolp binding) (fboundp binding))
              (setq binding (funcall binding nil)))
          (setq events (append binding nil))))
    (setq unread-command-events (cdr events))
    (car events)))

(defun event-apply-alt-modifier (_ignore-prompt)
  "\\<function-key-map>Add the Alt modifier to the following event.
For example, type \\[event-apply-alt-modifier] & to enter Alt-&."
  (vector (event-apply-modifier (read-event-for-modifier) 'alt 22 "A-")))
(defun event-apply-super-modifier (_ignore-prompt)
  "\\<function-key-map>Add the Super modifier to the following event.
For example, type \\[event-apply-super-modifier] & to enter Super-&."
  (vector (event-apply-modifier (read-event-for-modifier) 'super 23 "s-")))
(defun event-apply-hyper-modifier (_ignore-prompt)
  "\\<function-key-map>Add the Hyper modifier to the following event.
For example, type \\[event-apply-hyper-modifier] & to enter Hyper-&."
  (vector (event-apply-modifier (read-event-for-modifier) 'hyper 24 "H-")))
(defun event-apply-shift-modifier (_ignore-prompt)
  "\\<function-key-map>Add the Shift modifier to the following event.
For example, type \\[event-apply-shift-modifier] & to enter Shift-&."
  (vector (event-apply-modifier (read-event-for-modifier) 'shift 25 "S-")))
(defun event-apply-control-modifier (_ignore-prompt)
  "\\<function-key-map>Add the Ctrl modifier to the following event.
For example, type \\[event-apply-control-modifier] & to enter Ctrl-&."
  (vector (event-apply-modifier (read-event-for-modifier) 'control 26 "C-")))
(defun event-apply-meta-modifier (_ignore-prompt)
  "\\<function-key-map>Add the Meta modifier to the following event.
For example, type \\[event-apply-meta-modifier] & to enter Meta-&."
  (vector (event-apply-modifier (read-event-for-modifier) 'meta 27 "M-")))

(defun synthesize-keysym (ignore-prompt)
  "\\<function-key-map>Read named event"
  (vector (intern (read-string "Keysym: "))))

(define-key function-key-map (kbd "C-x @ k") 'synthesize-keysym)

(defun event-recursive-function-key-map (ignore-prompt)
  "\\<function-key-map>Return the next key event, and apply `function-key-map' as long as there is a binding.
Useful when a keysym entered via `synthesize-keysym' itself has an entry, possibly with shift."
  (let ((save-prefix-arg prefix-arg)
        (event (read-event-for-modifier))
        base-event modifiers binding)
    (while (cond ((numberp event)
                  (setq base-event (logand event #x840FFFF) ;(meta alt unicode)
                        modifiers (logxor event base-event)
                        binding (lookup-key function-key-map (vector base-event))))
                 ((symbolp event)
                  (setq modifiers (internal-event-symbol-parse-modifiers event)
                        base-event (list (pop modifiers)))
                  (dolist (base-shift '(shift alt))
                    (if (memq base-shift modifiers)
                      (setq modifiers (delq base-shift modifiers)
                            base-event (cons base-shift base-event))))
                  (setq binding (lookup-key function-key-map (vector base-event)))))
      (if (and (symbolp binding) (fboundp binding))
          (setq binding (funcall binding nil)))
      (setq event (append binding nil)
            unread-command-events (cdr event)
            event (car event))
      (if (and (numberp modifiers) (numberp event))
          (setq event (logior modifiers event))
        (if (numberp modifiers)
            (let ((lmodifiers nil))
              (dolist (map '((alt 22)
                             (super 23)
                             (hyper 24)
                             (shift 25)
                             (control 26)
                             (meta 27)))
                (if (not (zerop (logand modifiers (lsh 1 (cadr map)))))
                    (setq lmodifiers (cons (car map) lmodifiers))))
              (setq modifiers lmodifiers)))
        (setq event (event-convert-list (append modifiers (list event))))))
    (setq prefix-arg save-prefix-arg)
    (vector event)))

(define-key function-key-map [?\C-x ?@ ?q] 'event-recursive-function-key-map)

(defun lmkbd-graphic-unicode (key shifts code)
  "Register Unicode graphic character for possible shifted key.
Key can be either a keysym symbol or a base character code."
  (if (and (symbolp key) (not shifts))
      (put key 'ascii-character code))
  (define-key function-key-map (vector (if shifts (append shifts (list key)) key)) (vector code)))

)
)

;; Some of these Unicode characters do not correspond to anything in a
;; character set that un-define knows about.  They get lost when this
;; file is loaded.
(dolist (key '((alpha #x03B1 #x0391)    ;α Α
               (approximate #x2248)     ;≈
               (atsign #x0040 #x0060)   ;@ `
               (backslash #x005C #x007B) ;\ {
               (beta #x03B2 #x0392)     ;β Β
               (braceleft #x007B)       ;{
               (braceright #x007D)      ;}
               (bracketleft #x005B)     ;[
               (bracketright #x005D)    ;]
               ;; Each of these is only half of the legend.  Is there really a
               ;; graphic char on this keyboard with no corresponding single
               ;; Unicode glyph?
               (broketbottomleft #x231E) ;⌞
               (broketbottomright #x231F) ;⌟
               (brokettopleft #x231C)   ;⌜
               (brokettopright #x231D)  ;⌝
               (caret #x005E #x007E)    ;^ ~
               (ceiling #x2308)         ;⌈
               (cent #x00A2)            ;¢
               (chi #x03C7 #x03A7)      ;χ Χ
               (circle #x25CB)          ;○
               (circleminus #x2296)     ;⊖
               (circleplus #x2295)      ;⊕
               (circleslash #x2298)     ;⊘
               (circletimes #x2297)     ;⊗
               (colon #x003A #x002A)    ;: *
               (contained #x2283)       ;⊃
               (dagger #x2020)          ;†
               (degree #x00B0)          ;°
               (del #x2207)             ;∇
               (delta #x03b4 #x0394)    ;δ Δ
               (division #x00F7)        ;÷
               (doubbaselinedot #x00A8) ;¨
               (doublearrow #x2194)     ;↔
               (doublebracketleft #x27E6) ;⟦
               (doublebracketright #x27E7) ;⟧
               (doubledagger #x2021)    ;‡
               (doublevertbar #x2016)   ;‖
               (downarrow #x2193)       ;↓
               (downtack #x22A4)        ;⊤
               (epsilon #x03B5 #x0395)  ;ε Ε
               (eta #x03B7 #x0397)      ;η Η
               (exists #x2203)          ;∃
               (floor #x230A)           ;⌊
               (forall #x2200)          ;∀
               (gamma #x03B3 #x0393)    ;γ Γ
               (greaterthanequal #x2265) ;≥
               (guillemotleft #x00AB)   ;«
               (guillemotright #x00BB)  ;»
               (horizbar #x2015)        ;―
               (identical #x2261)       ;≡
               (includes #x2282)        ;⊂
               (infinity #x221E)        ;∞
               (integral #x222B)        ;∫
               (intersection #x2229)    ;∩
               (iota #x03B9 #x0399)     ;ι Ι
               (kappa #x03BA #x039A)    ;κ Κ
               (lambda #x03BB #x039B)   ;λ Λ
               (leftanglebracket #x2039) ;‹
               (leftarrow #x2190)       ;←
               (lefttack #x22A3)        ;⊣
               (lessthanequal #x2264)   ;≤
               (logicaland #x2227)      ;∧
               (logicalor #x2228)       ;∨
               (mu #x03BC #x039C)       ;μ Μ
               (notequal #x2260)        ;≠
               (notsign #x2310)         ;⌐
               (nu #x03BD #x039D)       ;ν Ν
               (omega #x03C9 #x03A9)    ;ω Ω
               (omicron #x03BF #x039F)  ;ο Ο
               (paragraph #x00B6)       ;¶
               (parenleft #x0028 #x005B) ;( [
               (parenright #x0029 #x005D) ;) ]
               (partialderivative #x2202) ;∂
               (periodcentered #x00B7)  ;·
               (phi #x03C6 #x03A6)      ;φ Φ
               (pi #x03C0 #x03A0)       ;π Π
               (plusminus #x00B1)       ;±
               (psi #x03C8 #x03A8)      ;ψ Ψ
               (quad #x2395)            ;⎕
               (rho #x03C1 #x03A1)      ;ρ Ρ
               (rightanglebracket #x203A) ;›
               (rightarrow #x2192)      ;→
               (righttack #x22A2)       ;⊢
               (section #x00A7)         ;§
               (sigma #x03C3 #x03A3)    ;σ Σ
               (similarequal #x2243)    ;≃
               (tau #x03C4 #x03A4)      ;τ Τ
               (theta #x03B8 #x0398)    ;θ Θ
               (times #x00D7)           ;×
               (union #x222A)           ;∪
               (uparrow #x2191)         ;↑
               (upsilon #x03C5 #x03A5)  ;υ Υ
               (uptack #x22A5)          ;⊥
               (varsigma #x03C2)        ;ς
               (vartheta #x03D1)        ;ϑ
               (vertbar #x007C #x007D)  ;| }
               (xi #x03BE #x039E)       ;ξ Ξ
               (zeta #x03B6 #x0396)     ;ζ Ζ

               (aplalpha #x237A)        ;⍺
               (apldelta #x2206)        ;∆
               (aplepsilon #x2208)      ;∈
               (apliota #x2373)         ;⍳
               (aplomega #x2375)        ;⍵
               (aplrho #x2374)          ;⍴
               ))
  (let ((keysym (car key))
        (code (cadr key))
        (shifted-code (car (cddr key))))
    (lmkbd-graphic-unicode keysym nil code)
    (if shifted-code
        (lmkbd-graphic-unicode keysym '(shift) shifted-code))))

(dolist (key '((clearscreen [(control ?l)])
               (clearinput [(control ?0) (control ?k)])
               (help [(control ?h)])
               (line [(control ?j)])
               (refresh [(control ?l)])
               (scroll [(control ?v)])
               ((shift scroll) [(meta ?v)])
               ))
  (global-set-key (vector (car key)) (cadr key)))

;; Make C-X <shift-2> consistent no matter which map.
(define-key function-key-map [?\C-x ?\"] (lookup-key function-key-map [?\C-x ?@]))
