# The Mecrisp Pocket board

An imaginary RV32IM microcontroller that runs Mecrisp-Quintus inside the app.
All device registers are 32-bit and word-accessed. Unmapped device addresses
read 0 and ignore writes.

## CPU

RV32IM with the CSR instructions, machine mode only, little-endian.
`fence` is a no-op. `ecall`, `ebreak`, illegal instructions and access faults
trap with the standard cause codes. Misaligned loads and stores work.

Implemented CSRs: `mstatus` (MIE, MPIE), `misa`, `mie`, `mip`, `mtvec`
(direct and vectored), `mepc`, `mcause`, `mtval`, `mscratch`, `mhartid` (0),
and the `cycle`, `time`, `instret` counters with their high halves.
`cycle` counts a nominal 100 MHz clock derived from real time;
`time` counts microseconds.

`wfi` sleeps until an interrupt is pending and enabled in `mie`, or until any
other event (new input, Stop, Reset). A waiting prompt uses no CPU.

## Memory map

| Region | Addresses | Size | Notes |
| --- | --- | --- | --- |
| Flash, core | `0x0000_0000`–`0x0000_7FFF` | 32 KiB | Quintus core; never writable |
| Flash, user dictionary | `0x0000_8000`–`0x0003_FFFF` | 224 KiB | `compiletoflash` area, saved to a file |
| RAM | `0x0004_0000`–`0x0007_FFFF` | 256 KiB | Stacks, RAM dictionary, buffers |
| Devices | `0x4001_0000`–`0x400C_FFFF` | 64 KiB per device | Below |

Like real flash, the flash region ignores plain stores. It changes only
through the flash write port, so a runaway program cannot damage saved words.

## Devices

| Address | Register | Access | Meaning |
| --- | --- | --- | --- |
| `0x4001_0000` | UART DATA | R/W | Read: next received byte. Write: send a byte. |
| `0x4002_0000` | UART FLAGS | R | Bit 8: byte received. Bit 9: transmitter busy (output buffer full). |
| `0x4003_0000` | FLASH ADDR | R/W | Address for the next flash write |
| `0x4003_0004` | FLASH WRITE32 | W | Programs a word at FLASH ADDR |
| `0x4003_0008` | FLASH WRITE16 | W | Programs a halfword at FLASH ADDR |
| `0x4003_000C` | FLASH ERASE | W | Erases (fills with `$FF`) from the written address to the end of flash |
| `0x4004_0000` | SYS ID | R | `0x5150_4B54` ("QPKT") |
| `0x4004_0004` | SYS VERSION | R | Board revision, 1 |
| `0x4004_0008` | SYS RESET CAUSE | R | 0 power-on, 1 Reset button, 2 software reset |
| `0x4004_000C` | SYS RESET | W | Write 1 for a full software reset |
| `0x4004_0010` | SYS SAFE BOOT | R | 1 after a safe-boot Reset: `init` is skipped |
| `0x4005_0000` | IRQ PENDING | R | Bit 0 break (Stop), 1 UART byte received, 2 audio done, 3 GPIO event |
| `0x4005_0004` | IRQ ENABLE | R/W | Same bits; reset value 1 (break only) |
| `0x4005_0008` | IRQ ACK | W | Write 1 to clear a pending bit |
| `0x4006_0000` | MTIME LO/HI | R | Microseconds since VM start (`+0`, `+4`) |
| `0x4006_0008` | MTIMECMP LO/HI | R/W | Timer interrupt when MTIME ≥ MTIMECMP (`+8`, `+C`) |
| `0x4007_0000` | Vibrator | | Milestone 4 |
| `0x4008_0000` | Display | | Milestone 4 |
| `0x4009_0000` | Audio | | Milestone 4 |
| `0x400A_0000` | Torch | | Reserved |
| `0x400B_0000` | Sensors | | Reserved |
| `0x400C_0000` | GPIO | | Milestone 4 |

IRQ PENDING AND IRQ ENABLE drives the machine external interrupt; the timer
drives the machine timer interrupt.

## Port behaviour

- **Stop** raises the break interrupt. The port's trap handler prints ` Stop.`
  and returns to `quit`, which clears both stacks and resets `base` to decimal.
  This needs interrupts enabled: after `dint`, use Reset.
- **Exceptions** (illegal instruction, bad address, `ecall`, `ebreak`) print
  `Exception mcause: … mepc: …` and return to the prompt.
- **Reset** keeps flash words and clears RAM. A long press (safe boot) skips `init`.
- **`eraseflash`** erases the whole user dictionary and resets;
  `eraseflashfrom ( addr -- )` erases from `addr` on.
- `key` sleeps with `wfi` while no input is waiting, unless a multitasker has
  replaced `hook-pause`.
