"""Reject a PSRAM app image that cannot safely enter its SRAM bootstrap."""

from __future__ import annotations

import pathlib
import re
import struct
import subprocess
import sys


PSRAM = range(0x11000000, 0x11800000)
SRAM = range(0x20000000, 0x20070000)
STACK_MARGIN = 8 * 1024


def symbols(elf: str, objdump: str) -> dict[str, int]:
    output = subprocess.check_output([objdump, "-t", elf], text=True)
    result: dict[str, int] = {}
    for line in output.splitlines():
        match = re.match(r"^([0-9a-fA-F]+)\s+.*\s(\S+)$", line)
        if match:
            result[match.group(2)] = int(match.group(1), 16)
    return result


def require_region(found: dict[str, int], name: str, region: range) -> None:
    address = found.get(name)
    if address is None or address not in region:
        where = "missing" if address is None else f"0x{address:08x}"
        raise SystemExit(f"layout check: {name} is {where}, outside its required region")


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit("usage: verify_psram_layout.py APP.elf OBJDUMP APP.uf2")
    found = symbols(sys.argv[1], sys.argv[2])
    for name in ("__vectors", "_entry_point", "main"):
        require_region(found, name, PSRAM)
    for name in ("fw2_psram_bootstrap", "board_init_psram", "psram_reinitialize"):
        require_region(found, name, SRAM)
    if not (SRAM.start < found.get("__StackTop", 0) <= SRAM.stop):
        raise SystemExit("layout check: initial stack is outside SRAM")
    if found["__vectors"] != PSRAM.start:
        raise SystemExit("layout check: vector table is not first in PSRAM")

    bss_end = found.get("__bss_end__")
    if bss_end is None or bss_end + STACK_MARGIN > found["__StackTop"]:
        raise SystemExit(
            "layout check: SRAM static storage leaves less than "
            f"{STACK_MARGIN} bytes below the initial stack"
        )

    uf2 = pathlib.Path(sys.argv[3]).read_bytes()
    if not uf2 or len(uf2) % 512:
        raise SystemExit("layout check: malformed UF2 length")
    vector = None
    for offset in range(0, len(uf2), 512):
        block = uf2[offset : offset + 512]
        magic0, magic1, _flags, address, size = struct.unpack_from("<5I", block)
        if (magic0, magic1, struct.unpack_from("<I", block, 508)[0]) != (
            0x0A324655,
            0x9E5D5157,
            0x0AB16F30,
        ):
            raise SystemExit("layout check: malformed UF2 magic")
        if not size or size > 476 or address not in PSRAM or address + size - 1 not in PSRAM:
            raise SystemExit(f"layout check: non-PSRAM payload at 0x{address:08x}")
        if address == PSRAM.start:
            vector = block[32 : 32 + size]
    if vector is None or len(vector) < 8:
        raise SystemExit("layout check: no PSRAM vector table")
    initial_sp, reset = struct.unpack_from("<2I", vector)
    if not (SRAM.start < initial_sp <= SRAM.stop) or not (reset & 1) or (reset & ~1) not in PSRAM:
        raise SystemExit("layout check: invalid SRAM stack or PSRAM reset vector")
    print(f"layout check: PSRAM image; {found['__StackTop'] - bss_end} bytes before initial stack")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
