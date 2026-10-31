"""Small deterministic RV32IM runner for freestanding C driver unit tests.

Uses only the already installed ESP GCC and pyelftools. This tests driver C
control flow against explicit SDK doubles; it does not simulate ESP hardware,
FreeRTOS scheduling, WiFi, lwIP or the HTTP server.
"""
from pathlib import Path
import subprocess
from elftools.elf.elffile import ELFFile

GCC = Path(r"C:/Espressif/tools/riscv32-esp-elf/esp-14.2.0_20260121/riscv32-esp-elf/bin/riscv32-esp-elf-gcc.exe")


def compile_elf(sources, includes, output):
    output = Path(output)
    output.parent.mkdir(exist_ok=True, parents=True)
    command = [str(GCC), "-march=rv32im", "-mabi=ilp32", "-Os", "-ffreestanding",
               "-fno-builtin", "-fno-stack-protector", "-msmall-data-limit=0", "-nostdlib",
               "-Wall", "-Wextra", "-Werror", "-Wl,-Ttext=0x10000", "-Wl,-e,0x10000",
               "-Wl,--no-relax"]
    for include in includes:
        command += ["-I", str(include)]
    subprocess.run(command + [str(x) for x in sources] + ["-o", str(output)], check=True)
    return RV32(output)


def signed(value, width=32):
    value &= (1 << width) - 1
    return value - (1 << width) if value & (1 << (width - 1)) else value


class RV32:
    def __init__(self, elf_path):
        self.initial = bytearray(1024 * 1024)
        with open(elf_path, "rb") as stream:
            elf = ELFFile(stream)
            for segment in elf.iter_segments():
                if segment["p_type"] == "PT_LOAD":
                    start = segment["p_vaddr"]
                    data = segment.data()
                    self.initial[start:start + len(data)] = data
            self.symbols = {symbol.name: symbol["st_value"]
                            for symbol in elf.get_section_by_name(".symtab").iter_symbols()}

    def call(self, name):
        # Each test starts with a clean process image, just like a fresh test binary.
        memory = bytearray(self.initial)
        registers = [0] * 32
        registers[1], registers[2] = 0xFFFFFFFC, 0xF0000
        pc = self.symbols[name]
        mask = 0xFFFFFFFF

        def read(address, size):
            if not 0 <= address <= len(memory) - size:
                raise RuntimeError(f"out-of-range read {address:#x}")
            return int.from_bytes(memory[address:address + size], "little")

        for _ in range(5_000_000):
            if pc == 0xFFFFFFFC:
                return signed(registers[10])
            instruction = read(pc, 4)
            opcode = instruction & 127
            rd, f3 = (instruction >> 7) & 31, (instruction >> 12) & 7
            rs1, rs2, f7 = (instruction >> 15) & 31, (instruction >> 20) & 31, instruction >> 25
            a, b = registers[rs1], registers[rs2]
            next_pc, value = pc + 4, None
            immediate = signed(instruction >> 20, 12)
            if opcode == 0x37:  # LUI
                value = instruction & 0xFFFFF000
            elif opcode == 0x17:  # AUIPC
                value = pc + (instruction & 0xFFFFF000)
            elif opcode == 0x6F:  # JAL
                offset = ((instruction >> 31) << 20) | (((instruction >> 12) & 255) << 12)
                offset |= ((instruction >> 20) & 1) << 11 | ((instruction >> 21) & 1023) << 1
                value, next_pc = pc + 4, pc + signed(offset, 21)
            elif opcode == 0x67:  # JALR
                value, next_pc = pc + 4, (a + immediate) & mask & ~1
            elif opcode == 0x63:
                offset = ((instruction >> 31) << 12) | (((instruction >> 7) & 1) << 11)
                offset |= ((instruction >> 25) & 63) << 5 | ((instruction >> 8) & 15) << 1
                condition = {0: a == b, 1: a != b, 4: signed(a) < signed(b),
                             5: signed(a) >= signed(b), 6: a < b, 7: a >= b}[f3]
                if condition:
                    next_pc = pc + signed(offset, 13)
            elif opcode == 0x03:
                size = {0: 1, 1: 2, 2: 4, 4: 1, 5: 2}[f3]
                value = read((a + immediate) & mask, size)
                if f3 < 3:
                    value = signed(value, size * 8)
            elif opcode == 0x23:
                offset = signed(((instruction >> 25) << 5) | ((instruction >> 7) & 31), 12)
                address, size = (a + offset) & mask, {0: 1, 1: 2, 2: 4}[f3]
                if not 0 <= address <= len(memory) - size:
                    raise RuntimeError(f"out-of-range write {address:#x}")
                memory[address:address + size] = (b & ((1 << (size * 8)) - 1)).to_bytes(size, "little")
            elif opcode == 0x13:
                if f3 == 0: value = a + immediate
                elif f3 == 2: value = int(signed(a) < immediate)
                elif f3 == 3: value = int(a < (immediate & mask))
                elif f3 == 4: value = a ^ (immediate & mask)
                elif f3 == 6: value = a | (immediate & mask)
                elif f3 == 7: value = a & (immediate & mask)
                elif f3 == 1: value = a << (rs2 & 31)
                elif f3 == 5: value = (signed(a) if f7 == 32 else a) >> (rs2 & 31)
            elif opcode == 0x33:
                if f7 == 1:
                    if f3 == 0: value = a * b
                    elif f3 == 1: value = (signed(a) * signed(b)) >> 32
                    elif f3 == 2: value = (signed(a) * b) >> 32
                    elif f3 == 3: value = (a * b) >> 32
                    elif f3 == 4:
                        value = -1 if b == 0 else abs(signed(a)) // abs(signed(b)) * (-1 if (signed(a) < 0) != (signed(b) < 0) else 1)
                    elif f3 == 5: value = mask if b == 0 else a // b
                    elif f3 == 6:
                        quotient = 0 if b == 0 else abs(signed(a)) // abs(signed(b)) * (-1 if (signed(a) < 0) != (signed(b) < 0) else 1)
                        value = signed(a) if b == 0 else signed(a) - quotient * signed(b)
                    elif f3 == 7: value = a if b == 0 else a % b
                elif f3 == 0: value = a - b if f7 == 32 else a + b
                elif f3 == 1: value = a << (b & 31)
                elif f3 == 2: value = int(signed(a) < signed(b))
                elif f3 == 3: value = int(a < b)
                elif f3 == 4: value = a ^ b
                elif f3 == 5: value = (signed(a) if f7 == 32 else a) >> (b & 31)
                elif f3 == 6: value = a | b
                elif f3 == 7: value = a & b
            elif opcode == 0x0F:  # FENCE, no-op in this single-thread runner
                pass
            else:
                raise RuntimeError(f"unsupported RV32 opcode at {pc:#x}: {instruction:#x}")
            if value is not None and rd:
                registers[rd] = value & mask
            registers[0], pc = 0, next_pc & mask
        raise RuntimeError(f"instruction budget exhausted in {name}")
