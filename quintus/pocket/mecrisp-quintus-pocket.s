#
#    Mecrisp-Quintus - A native code Forth implementation for RISC-V
#    Copyright (C) 2018  Matthias Koch
#
#    Mecrisp Pocket board port, based on the ulx3s-pipeline target.
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

# -----------------------------------------------------------------------------
# Switches for capabilities of this board: RV32IM, no compressed instructions
# -----------------------------------------------------------------------------

.option norelax

# -----------------------------------------------------------------------------
# Memory map for Flash and RAM (see docs/board.md)
# -----------------------------------------------------------------------------

.equ RamAnfang, 0x00040000 # Start of RAM
.equ RamEnde,   0x00080000 # End   of RAM. 256 KiB

.equ FlashAnfang, 0x00000000 # Start of Flash
.equ FlashEnde,   0x00040000 # End   of Flash. 256 KiB

.equ FlashDictionaryAnfang, FlashAnfang + 0x8000 # 32 KiB reserved for core.
.equ FlashDictionaryEnde,   FlashEnde            # 224 KiB user flash dictionary

# -----------------------------------------------------------------------------
# Board registers used by the core (see docs/board.md)
# -----------------------------------------------------------------------------

.equ uart_data,     0x40010000
.equ uart_flags,    0x40020000

.equ SYS_BASE,      0x40040000
.equ SYS_RESET,     SYS_BASE + 0x0C
.equ SYS_SAFEBOOT,  SYS_BASE + 0x10

.equ IRQ_BASE,      0x40050000
.equ IRQ_PENDING,   IRQ_BASE + 0x0
.equ IRQ_ENABLE,    IRQ_BASE + 0x4
.equ IRQ_ACK,       IRQ_BASE + 0x8

# -----------------------------------------------------------------------------
# Core start
# -----------------------------------------------------------------------------

.text
  j Reset

# -----------------------------------------------------------------------------
# Include the Forth core of Mecrisp-Quintus
# -----------------------------------------------------------------------------

.include "../common/forth-core.s"

# -----------------------------------------------------------------------------
Reset: # Forth begins here
# -----------------------------------------------------------------------------

  .include "../common/catchflashpointers.s"

  call uart_init

  welcome " for RISC-V RV32IM on Mecrisp Pocket"

  # Safe boot: skip the init word, go straight to the prompt.
  li x15, SYS_SAFEBOOT
  lw x15, 0(x15)
  beq x15, zero, 1f
    writeln "Safe boot: init skipped."
    j quit
1:

  # Ready to fly !
  .include "../common/boot.s"

.org FlashDictionaryAnfang, 0xFF # Core must stay below the user dictionary
.org FlashEnde, 0xFF
