import pefile
import capstone


PATH = r"C:\WINDOWS\GenericFFBDriver\GenericFFBDriver64.dll"
data = open(PATH, "rb").read()
pe = pefile.PE(data=data)
base = pe.OPTIONAL_HEADER.ImageBase
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True


def off_from_va(va):
    return pe.get_offset_from_rva(va - base)


for start, end in [
    (0x180002E80, 0x180003100),
    (0x180006350, 0x180006650),
    (0x180005580, 0x180005620),
]:
    print(f"--- disasm {start:x}-{end:x}")
    code = data[off_from_va(start) : off_from_va(end)]
    for ins in md.disasm(code, start):
        print(f"{ins.address:016x}: {ins.mnemonic:8} {ins.op_str}")

for va in [
    0x180042DDE,
    0x180042CF4,
    0x180042DC2,
    0x18003FD80,
    0x18003FD88,
    0x18003FD98,
    0x18003FDB0,
]:
    off = off_from_va(va)
    bs = data[off : off + 64]
    print(f"--- data VA {va:x} off {off:x}")
    print(" ".join(f"{b:02X}" for b in bs))
    print(repr(bs[:32]))
