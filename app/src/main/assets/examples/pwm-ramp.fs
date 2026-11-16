\ PWM ramp for Mecrisp Pocket
\
\ Software PWM on one pin: the duty cycle rises from 0 to 100 % in
\ 10 s, falls back to 0 in 10 s, and so on. The period is 10 ms, in
\ steps of 100 us (1 %), which is the GPIO trace's resolution.
\
\ Send this file (Send > Whole file), then type 0 pwm-ramp in the REPL
\ (any pin 0-15 works) and open the GPIO tab: the 50 ms window shows
\ each pulse growing, the 500 ms window the ramp. To stop it, press
\ Send in the REPL with an empty line (or press Stop).

compiletoram

0 variable pwm-pin
0 variable pwm-t                  \ start of the current period, in cycles

1000000 constant pwm-period       \ 10 ms at 100 MHz
  10000 constant pwm-step         \ 100 us = 1 % of the period

\ Waits until the cycle counter passes t. Every edge is timed from the
\ start of its period, so small delays do not add up.
: until-cycles ( t -- )  begin dup cycles - 0< until drop ;

: pwm-cycle ( duty -- )           \ one period, high for duty %
  dup if pwm-pin @ pin-high then
  dup pwm-step * pwm-t @ + until-cycles
  100 < if pwm-pin @ pin-low then
  pwm-t @ pwm-period + dup pwm-t ! until-cycles ;

: pwm-level ( duty -- )  10 0 do dup pwm-cycle loop drop ;   \ 100 ms

: ramp-duty ( step -- duty )  dup 100 > if 200 swap - then ;  \ 0..199 -> 0..100..1

: pwm-ramp ( pin -- )
  dup pwm-pin !  dup pin-out  pin-low
  cycles pwm-t !
  cr ." Pin " pwm-pin @ . ." ramps from 0 to 100 % in 10 s and back."
  cr ." Send an empty line to stop."
  0 begin
    dup ramp-duty pwm-level
    1+ 200 mod
  key? until drop
  pwm-pin @ pin-low  cr ." Stopped." ;
