#
#    Mecrisp-Quintus - A native code Forth implementation for RISC-V
#    Copyright (C) 2018  Matthias Koch
#
#    Mecrisp Pocket terminal and trap handling, based on the
#    ulx3s-pipeline target.
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

.include "../common/interrupt-femtorv.s"
.include "../common/terminalhooks.s"

# -----------------------------------------------------------------------------
uart_init:
# -----------------------------------------------------------------------------
  la x15, pocket_trap
  csrrw zero, mtvec, x15  # All traps enter through pocket_trap

  li x15, 0x800           # MEIE: machine external interrupt enable
  csrrs zero, mie, x15
  csrrsi zero, mstatus, 8 # MIE, so that the Stop button can break in
  ret

# -----------------------------------------------------------------------------
# Trap entry. The Stop button raises IRQ bit 0 (break) on the machine
# external interrupt: return to quit with clean stacks. Everything else
# goes to the standard irq-collection hook with all registers intact.
# -----------------------------------------------------------------------------

.align 2
pocket_trap:
  addi sp, sp, -8
  sw x14, 0(sp)
  sw x15, 4(sp)

  csrrs x15, mcause, zero
  bge x15, zero, 3f       # Exception, not an interrupt
  li x14, 0x8000000B      # Interrupt, cause 11: machine external
  bne x15, x14, 1f

  li x14, IRQ_PENDING
  lw x15, 0(x14)
  andi x15, x15, 1        # Break pending ?
  beq x15, zero, 1f

  li x15, 1
  li x14, IRQ_ACK
  sw x15, 0(x14)          # Acknowledge break

  la x15, pocket_break
  csrrw zero, mepc, x15   # Resume in pocket_break, outside trap context
  li x15, 0x80
  csrrs zero, mstatus, x15 # MPIE: interrupts enabled again after mret
  mret

pocket_break:
  laf sp, returnstackanfang
  laf x9, datenstackanfang
  writeln " Stop."
  j quit                  # quit resets stacks, base, state and input

1:lw x14, 0(sp)
  lw x15, 4(sp)
  addi sp, sp, 8
  j irq_collection

3:# Exceptions (illegal instruction, ecall, ebreak, ...) would trap again
  # forever after returning. Leave trap context and report instead.
  csrrs x15, mepc, zero
  csrrw zero, mscratch, x15 # Keep the faulting address for the report
  la x15, pocket_exception
  csrrw zero, mepc, x15
  li x15, 0x80
  csrrs zero, mstatus, x15 # MPIE: interrupts enabled again after mret
  mret

pocket_exception:
  laf sp, returnstackanfang
  laf x9, datenstackanfang
  write "Exception mcause: "
  call mcause_fetch
  call hexdot
  write "mepc: "
  pushdatos
  csrrs x8, mscratch, zero
  call hexdot
  writeln "!"
  j quit

# -----------------------------------------------------------------------------
  Definition Flag_visible, "serial-emit"
serial_emit: # ( c -- ) Emit one character
# -----------------------------------------------------------------------------
  push x1

1:call serial_qemit
  popda x15
  beq x15, zero, 1b

  li x14, uart_data
  sw x8, 0(x14)
  drop

  pop x1
  ret

# -----------------------------------------------------------------------------
  Definition Flag_visible, "serial-key"
serial_key: # ( -- c ) Receive one character
# -----------------------------------------------------------------------------
  push x1

1:call serial_qkey
  popda x15
  bne x15, zero, 2f

  # Nothing received. Sleep until something happens, unless a multitasker
  # has replaced the pause hook and other tasks need the CPU.
  laf x15, hook_pause
  lw x15, 0(x15)
  la x14, nop_vektor
  bne x15, x14, 1b
  wfi
  j 1b

2:pushdatos
  li x14, uart_data
  lw x8, 0(x14)
  andi x8, x8, 0xFF

  pop x1
  ret

# -----------------------------------------------------------------------------
  Definition Flag_visible, "serial-emit?"
serial_qemit:  # ( -- ? ) Ready to send a character ?
# -----------------------------------------------------------------------------
  push x1
  call pause

  pushdatos
  li x8, uart_flags
  lw x8, 0(x8)
  andi x8, x8, 0x200

  sltiu x8, x8, 1 # 0=
  sub x8, zero, x8

  pop x1
  ret

# -----------------------------------------------------------------------------
  Definition Flag_visible, "serial-key?"
serial_qkey:  # ( -- ? ) Is there a key press ?
# -----------------------------------------------------------------------------
  push x1
  call pause

  pushdatos
  li x8, uart_flags
  lw x8, 0(x8)
  andi x8, x8, 0x100

  sltiu x8, x8, 1 # 0<>
  addi x8, x8, -1

  pop x1
  ret

# -----------------------------------------------------------------------------
  Definition Flag_visible, "reset"
pocket_reset: # Full software reset through the board
# -----------------------------------------------------------------------------
  csrrci zero, mstatus, 8 # Clear Machine Interrupt Enable Bit
  li x14, SYS_RESET
  li x15, 1
  sw x15, 0(x14)
  j Reset                 # Not reached on the emulator

# Phone devices and GPIO pins
.include "devices.s"
