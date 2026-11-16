#
#    Mecrisp Pocket - words for the board's phone devices and GPIO pins.
#    Copyright (C) 2026 The Mecrisp Pocket authors.
#
#    This program is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

# The registers are described in docs/board.md. These words live in the core,
# so eraseflash keeps them. They use only x14 and x15 (and the stack).

.equ VIB_TIME,        0x40070000 # W: vibrate n ms (0 stops). R: ms left
.equ DISP_BRIGHTNESS, 0x40080000 # 0..255, or -1 for the system setting
.equ AUDIO_FREQ,      0x40090000 # Hz
.equ AUDIO_TIME,      0x40090004 # W: play n ms (0 stops). R: ms left
.equ AUDIO_WAVE,      0x40090008 # 0 sine, 1 square, 2 triangle, 3 sawtooth
.equ AUDIO_VOLUME,    0x4009000C # 0..100
.equ GPIO_DIR,        0x400C0000 # 1 = output
.equ GPIO_OUT,        0x400C0004 # Output latch
.equ GPIO_IN,         0x400C0008 # Pin levels
.equ GPIO_SET,        0x400C000C # Writing 1 bits sets them in OUT
.equ GPIO_CLR,        0x400C0010 # Writing 1 bits clears them in OUT
.equ GPIO_TOGGLE,     0x400C0014 # Writing 1 bits flips them in OUT

# -----------------------------------------------------------------------------
  Definition Flag_visible, "vibrate" # ( ms -- ) Vibrates for ms; 0 stops.
# -----------------------------------------------------------------------------
  li x15, VIB_TIME
  sw x8, 0(x15)
  drop
  ret

# -----------------------------------------------------------------------------
  Definition Flag_visible, "brightness" # ( n -- ) 0..255; -1: system setting
# -----------------------------------------------------------------------------
  li x15, DISP_BRIGHTNESS
  sw x8, 0(x15)
  drop
  ret

# -----------------------------------------------------------------------------
  Definition Flag_visible, "tone" # ( Hz ms -- ) Plays a tone; returns at once.
# -----------------------------------------------------------------------------
  lc x14, 0(x9)
  li x15, AUDIO_FREQ
  sw x14, 0(x15)
  sw x8, 4(x15)   # AUDIO_TIME: starts the tone
  lc x8, CELL(x9)
  addi x9, x9, 2*CELL
  ret

# -----------------------------------------------------------------------------
  Definition Flag_visible, "silence" # ( -- ) Stops the tone.
# -----------------------------------------------------------------------------
  li x15, AUDIO_TIME
  sw zero, 0(x15)
  ret

# -----------------------------------------------------------------------------
  Definition Flag_visible, "tone?" # ( -- ? ) True while a tone plays.
# -----------------------------------------------------------------------------
  pushdatos
  li x15, AUDIO_TIME
  lw x8, 0(x15)
  sltu x8, zero, x8
  sub x8, zero, x8
  ret

# -----------------------------------------------------------------------------
  Definition Flag_visible, "waveform" # ( n -- ) 0 sine 1 square 2 triangle 3 sawtooth
# -----------------------------------------------------------------------------
  li x15, AUDIO_WAVE
  sw x8, 0(x15)
  drop
  ret

# -----------------------------------------------------------------------------
  Definition Flag_visible, "volume" # ( n -- ) 0..100
# -----------------------------------------------------------------------------
  li x15, AUDIO_VOLUME
  sw x8, 0(x15)
  drop
  ret

# GPIO pins 0..15; higher pin numbers wrap around.

# -----------------------------------------------------------------------------
  Definition Flag_visible, "pin-out" # ( pin -- ) Makes the pin an output.
# -----------------------------------------------------------------------------
  andi x14, x8, 15
  li x15, 1
  sll x15, x15, x14
  li x14, GPIO_DIR
  lw x8, 0(x14)
  or x8, x8, x15
  sw x8, 0(x14)
  drop
  ret

# -----------------------------------------------------------------------------
  Definition Flag_visible, "pin-in" # ( pin -- ) Makes the pin an input.
# -----------------------------------------------------------------------------
  andi x14, x8, 15
  li x15, 1
  sll x15, x15, x14
  xori x15, x15, -1
  li x14, GPIO_DIR
  lw x8, 0(x14)
  and x8, x8, x15
  sw x8, 0(x14)
  drop
  ret

# -----------------------------------------------------------------------------
  Definition Flag_visible, "pin-high" # ( pin -- )
# -----------------------------------------------------------------------------
  andi x14, x8, 15
  li x15, 1
  sll x15, x15, x14
  li x14, GPIO_SET
  sw x15, 0(x14)
  drop
  ret

# -----------------------------------------------------------------------------
  Definition Flag_visible, "pin-low" # ( pin -- )
# -----------------------------------------------------------------------------
  andi x14, x8, 15
  li x15, 1
  sll x15, x15, x14
  li x14, GPIO_CLR
  sw x15, 0(x14)
  drop
  ret

# -----------------------------------------------------------------------------
  Definition Flag_visible, "pin-toggle" # ( pin -- )
# -----------------------------------------------------------------------------
  andi x14, x8, 15
  li x15, 1
  sll x15, x15, x14
  li x14, GPIO_TOGGLE
  sw x15, 0(x14)
  drop
  ret

# -----------------------------------------------------------------------------
  Definition Flag_visible, "pin@" # ( pin -- ? ) True if the pin is high.
# -----------------------------------------------------------------------------
  andi x14, x8, 15
  li x15, GPIO_IN
  lw x15, 0(x15)
  srl x15, x15, x14
  andi x15, x15, 1
  sub x8, zero, x15
  ret

# -----------------------------------------------------------------------------
  Definition Flag_visible, "pins@" # ( -- x ) Levels of all pins, pin 0 in bit 0.
# -----------------------------------------------------------------------------
  pushdatos
  li x15, GPIO_IN
  lw x8, 0(x15)
  ret

# -----------------------------------------------------------------------------
  Definition Flag_visible, "pins!" # ( x -- ) Sets all output levels at once.
# -----------------------------------------------------------------------------
  li x15, GPIO_OUT
  sw x8, 0(x15)
  drop
  ret
