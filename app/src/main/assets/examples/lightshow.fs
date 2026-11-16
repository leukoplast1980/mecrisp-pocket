\ Pocket lightshow for Mecrisp Pocket
\
\ Pins 0-7    a Knight Rider scanner
\ Pins 8-11   a binary counter of the sweeps
\ Pin 12      a breathing LED, made with software PWM
\ Pin 15      a button: tap P15 in the GPIO tab to change the mode
\ Sound       a blip on every step; its pitch follows the scanner
\ Screen      the brightness breathes along with pin 12
\
\ Send this file (Send > Whole file), then type lightshow in the REPL
\ and open the GPIO tab. To stop it, press Send in the REPL with an
\ empty line (or press Stop).

compiletoram

\ ---- Time: the cycle counter runs at 100 MHz ----

: us ( n -- ) 100 * cycles +  begin dup cycles - 0< until  drop ;
: ms ( n -- ) 0 ?do 1000 us loop ;

\ ---- Pins ----

: field! ( x mask -- )   \ sets only the pins in mask, to x
  pins@ over bic  -rot and  or pins! ;

: setup-pins ( -- )  13 0 do i pin-out loop  15 pin-in ;

\ In Mecrisp, variable takes its start value from the stack.
0 variable pos      \ scanner position, 0..7
1 variable dir      \ scanner direction, 1 or -1
0 variable sweeps   \ finished sweeps
0 variable mode     \ 0..3: waveform and speed
0 variable phase    \ breathing phase, 0..19
0 variable button   \ last level of pin 15

: scanner ( -- )  1 pos @ lshift  $ff field! ;
: counter ( -- )  sweeps @ 8 lshift  $f00 field! ;

: step-scanner ( -- )
  pos @ dir @ +  dup pos !
  dup 7 = swap 0= or if
    dir @ negate dir !  1 sweeps +!
    cr ." Sweep " sweeps @ .  ." mode " mode @ .
  then ;

\ ---- Breathing ----

: duty ( -- 0..10 )  phase @ dup 10 > if 20 swap - then ;
: breathe ( -- )  phase @ 1+ 20 mod phase ! ;

: pwm ( duty periods -- )   \ pin 12 high for duty tenths of each 1 ms
  0 ?do
    dup 0 ?do 12 pin-high 100 us loop
    10 over - 0 ?do 12 pin-low 100 us loop
  loop drop ;

\ ---- The button and the modes ----

: button? ( -- ? )  \ true when pin 15 changed: every tap counts
  15 pin@  dup button @ <>  swap button ! ;

: next-mode ( -- )
  mode @ 1+ 4 mod  dup mode !  dup waveform
  200 vibrate
  cr ." Mode " . ;

: step-ms ( -- n )  100 mode @ 25 * - ;   \ 100, 75, 50, 25 ms

\ ---- The show ----

: show-step ( -- )
  scanner counter
  pos @ 60 * 300 +  30 tone
  duty 15 * 100 + brightness
  duty step-ms pwm
  breathe
  step-scanner
  button? if next-mode then ;

: cleanup ( -- )  silence  -1 brightness  0 pins!  cr ." Show over." ;

: lightshow ( -- )
  setup-pins
  0 pos !  1 dir !  0 sweeps !  0 mode !  0 phase !  15 pin@ button !
  0 waveform  30 volume
  cr ." Lightshow: tap P15 in the GPIO tab to change the mode."
  cr ." Send an empty line to stop."
  begin show-step key? until
  cleanup ;
