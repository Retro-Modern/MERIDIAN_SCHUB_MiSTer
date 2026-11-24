# SCHUB – optional RISC-V co-processor for MERIDIAN

SCHUB adds a programmable RV32IM processor to the 65C816. It runs at
24 MHz with a 64 KiB instruction cache and a 32 KiB data cache. It uses the
existing MERIDIAN chips and the same 24-bit address space, and the 65C816
keeps running alongside it. SDRAM accesses go through a separate 16-bit
path; chip RAM and the chip registers share the existing byte bus. SCHUB has
no access to the ARM/Linux side of the MiSTer.

SCHUB is switched on and off in the MiSTer menu (**SCHUB: Off/On**). When it
is off, the machine behaves exactly like the original MERIDIAN 816.

The CPU is [VexRiscv](https://github.com/SpinalHDL/VexRiscv) (MIT license),
generated with SpinalHDL; see `rtl/schub/generated.json` for the exact
versions and `rtl/schub/LICENSE.VexRiscv`.

## Registers from $00:CD00

All multi-byte values are little endian. The registers and the ID stay
readable while SCHUB is switched off.

| Offset | Meaning |
| --- | --- |
| $00–$03 | ASCII `SCHB` |
| $04 | Interface version 1 |
| $05 | Capabilities: bit 0 RV32IM, 1 caches, 2 uncached alias, 3 SDRAM half-words, 4 KRAN column copy |
| $08 | Control: bit 0 enable, bit 1 run, bit 7 trigger restart |
| $09 | Status: bit 7 present, 3 address error, 2 bus busy, 1 running, 0 enabled |
| $0C–$0F | Start address; writable only while stopped, must be divisible by 4 |
| $10–$13 | Free argument register |
| $14–$17 | Free result register |
| $18–$1B | Cycle counter; reading $18 latches the upper bytes for this read |
| $1C–$1F | Clock frequency: 24,000,000 Hz |

Enabling SCHUB in the menu does not start whatever happens to be in memory.
Software first loads its code, sets the start address and writes 3 to
`$CD08`. This way a program can also enable SCHUB when it was off in the
menu. Changes made by software are reported back to the menu. If both change
it at the same time, the last one processed wins.

To stop, write 0 to `$CD08` and wait until status bits 1 and 2 are zero. A
memory transfer that has started is still completed; then CPU and caches are
reset. A restart with bit 7 also waits for the end of the transfer.
Switching off is not a pause of the RISC-V program. A 65C816 program can
watch the status and restore its display when SCHUB is switched off.

## Memory and working with the chips

RISC-V addresses `$00000000–$00FFFFFF` map to the MERIDIAN address space.
Bit 31 selects the uncached alias, e.g. `$8000C600` for KRAN. Any other set
bit 24–30 raises a bus error; addresses are not silently cut to 24 bits.
Cartridges in banks $40–$7F stay write-protected while a cartridge is
inserted.

Access registers, shared buffers and DMA data only through the uncached
alias. There is no automatic cache coherency with the 65C816, BOTE, KRAN,
LOTSE, PINSEL or ORGEL. Private code and private data may use the cache.
Load self-modifying code preferably while SCHUB is stopped, then restart it.

RV32IM has no compressed instructions and no floating-point unit. Unaligned
word accesses raise an exception. PFORTE provides the machine timer
interrupt; software must acknowledge its cause in PFORTE.

## KRAN extension

While SCHUB is enabled, mode bit 3 in copy mode 0 (`$C60F`) selects a column
copy: KRAN reads exactly one source byte per line and repeats it `WIDTH`
times. The next line follows the usual source and destination strides.
Backwards direction, job lists and the end bit 7 stay available.

While SCHUB is off, bit 3 is ignored and KRAN copies as before. Other copy
modes, fill and transparency keep their meaning. The mode is latched at the
start of a job, so switching does not change half a job.

## SCHUB from BASIC

MERIDIAN BASIC in this core knows one more word, `SCHUB` (token 242):

| | |
| --- | --- |
| `SCHUB adr` | stop, set start address `adr`, clear the mailbox, start (also switches SCHUB on) |
| `SCHUB 0` | stop and switch off |
| `SCHUB(n[,a[,b[,c]]])` | call function `n` of the running program, wait, return its 32-bit result |
| `SCHUB(-1)` | status byte `$CD09` |

The usual way is to build the RISC-V program as a MER file on a computer,
`BLOAD "NAME"` it (to `$04:0000` = 262144, for example) and start it with
`SCHUB 262144`. BASIC and the RISC-V program talk through a mailbox in the
system bank, which the RISC-V program reaches through the uncached alias:

| 65C816 | RISC-V | 32-bit word |
| --- | --- | --- |
| `$FE:1800` | `$80FE1800` | state: 0 free, 1 request (BASIC), 2 done (SCHUB) |
| `$FE:1804` | `$80FE1804` | `n` |
| `$FE:1808` | `$80FE1808` | `a` |
| `$FE:180C` | `$80FE180C` | `b` |
| `$FE:1810` | `$80FE1810` | `c` |
| `$FE:1814` | `$80FE1814` | result |

`SCHUB()` writes `n`, `a`, `b`, `c` (sign-extended, missing values 0), then
state 1, and waits for state 2. The RISC-V program waits for state 1, reads
the values, writes the result and then state 2; BASIC sets the state back
to 0. While BASIC waits it checks `Esc` and the status: if SCHUB stops or
reports an address error, `SCHUB()` ends with `?ILLEGAL QUANTITY`. After
`Esc` the request is withdrawn; restart the RISC-V program with
`SCHUB adr` if it may still answer the old request.

`tools/schubdemo.py` builds `programme/rvdemo.mer`, a complete example of
such a program (about 40 instructions, encoded in the script, so no RISC-V
toolchain is needed); `programme/schubdemo.bas` uses it.

The code is `rom/schub.asm` in the system ROM (entries `$FF:402D` and
`$FF:4030`, kernel command 29 and function 10); `basic/meridian.s` only
reads the values.

## Tests

- `sim/schub_test`: `make`, then `./obj_dir/control` runs real RV32IM code
  and checks switching off, software start, menu feedback, bus masks, cache
  restart, delayed memory replies, restart during a transfer and address
  errors. `./obj_dir/kran` checks the column copy and the existing copy and
  fill functions.
- `sim/`: the full simulation contains both processors. `MERIDIAN_SCHUB_OSD`
  ="frame:value;…" switches SCHUB like the menu.

## Register space kept free

`$00:CD00–$CD1F`, KRAN mode bit 3, the mailbox `$FE:1800–$FE:1817` and
the BASIC token 242 (`SCHUB`) are used by SCHUB. The original
MERIDIAN 816 keeps them free, so programs for the original run here
unchanged.
