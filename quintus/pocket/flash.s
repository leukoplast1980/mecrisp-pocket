#
#    Mecrisp-Quintus - A native code Forth implementation for RISC-V
#    Copyright (C) 2018  Matthias Koch
#
#    Mecrisp Pocket flash words. Like real flash, the flash region ignores
#    plain stores; it is written only through the flash write port, so a
#    runaway program cannot damage the saved dictionary. The board ignores
#    writes below FlashDictionaryAnfang, which protects the core.
#    eraseflash follows the gd32vf103cb target.
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

.equ FLASH_BASE,    0x40030000
.equ FLASH_ADDR,    FLASH_BASE + 0x0 # Address for the next write
.equ FLASH_WRITE32, FLASH_BASE + 0x4 # Writing here programs a word at FLASH_ADDR
.equ FLASH_WRITE16, FLASH_BASE + 0x8 # Writing here programs a halfword at FLASH_ADDR
.equ FLASH_ERASE,   FLASH_BASE + 0xC # Erases from the written address to the end

# -----------------------------------------------------------------------------
  Definition Flag_visible, "flash!"
flashstore: # ( x addr -- )
# -----------------------------------------------------------------------------
  li x14, FLASH_ADDR
  sw x8, 0(x14)
  drop
  li x14, FLASH_WRITE32
  sw x8, 0(x14)
  drop
  ret

# -----------------------------------------------------------------------------
  Definition Flag_visible, "hflash!"
hflashstore: # ( x addr -- )
# -----------------------------------------------------------------------------
  li x14, FLASH_ADDR
  sw x8, 0(x14)
  drop
  li x14, FLASH_WRITE16
  sw x8, 0(x14)
  drop
  ret

# -----------------------------------------------------------------------------
  Definition Flag_visible, "eraseflash" # ( -- )
  # Erases the whole user flash dictionary, then resets.
# -----------------------------------------------------------------------------
  li x10, FlashDictionaryAnfang

eraseflash_intern:
  li x14, FLASH_ERASE
  sw x10, 0(x14)

  writeln "Finished. Reset !"
  j pocket_reset

# -----------------------------------------------------------------------------
  Definition Flag_visible, "eraseflashfrom" # ( addr -- )
  # Erases the user flash dictionary from addr on, then resets.
# -----------------------------------------------------------------------------
  popda x10
  andi x10, x10, -4

  li x11, FlashDictionaryAnfang
  bgeu x10, x11, 1f
    writeln "Cannot erase the core !"
    j quit

1:li x11, FlashDictionaryEnde
  bltu x10, x11, eraseflash_intern
    writeln "Address is outside of flash !"
    j quit
