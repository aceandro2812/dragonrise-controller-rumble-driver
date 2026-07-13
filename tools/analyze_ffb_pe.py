import argparse
import json
import re
from collections import defaultdict

import capstone
import pefile


def read_bytes(path):
    with open(path, "rb") as f:
        return f.read()


def cstr(data, off):
    end = off
    while end < len(data) and data[end] != 0:
        end += 1
    return data[off:end].decode("ascii", errors="ignore")


def ascii_strings(data, min_len=5):
    for m in re.finditer(rb"[\x20-\x7e]{%d,}" % min_len, data):
        yield {"offset": m.start(), "text": m.group().decode("ascii", errors="replace")}


def section_for_rva(pe, rva):
    for s in pe.sections:
        start = s.VirtualAddress
        end = start + max(s.Misc_VirtualSize, s.SizeOfRawData)
        if start <= rva < end:
            return s
    return None


def disasm_text(pe, data, is_64):
    text = None
    for s in pe.sections:
        if s.Name.rstrip(b"\x00") == b".text":
            text = s
            break
    if not text:
        return []
    off = text.PointerToRawData
    size = text.SizeOfRawData
    va = pe.OPTIONAL_HEADER.ImageBase + text.VirtualAddress
    mode = capstone.CS_MODE_64 if is_64 else capstone.CS_MODE_32
    md = capstone.Cs(capstone.CS_ARCH_X86, mode)
    md.detail = True
    return list(md.disasm(data[off : off + size], va))


def imports(pe):
    out = []
    if not hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
        return out
    for entry in pe.DIRECTORY_ENTRY_IMPORT:
        dll = entry.dll.decode(errors="replace")
        for imp in entry.imports:
            name = imp.name.decode(errors="replace") if imp.name else f"ord_{imp.ordinal}"
            out.append({"dll": dll, "name": name, "iat": imp.address})
    return out


def exports(pe):
    out = []
    if not hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
        return out
    for sym in pe.DIRECTORY_ENTRY_EXPORT.symbols:
        out.append(
            {
                "ordinal": sym.ordinal,
                "name": sym.name.decode(errors="replace") if sym.name else "",
                "rva": sym.address,
                "va": pe.OPTIONAL_HEADER.ImageBase + sym.address,
            }
        )
    return out


def find_import_xrefs(insns, imps, is_64):
    by_va = {i["iat"]: i for i in imps}
    xrefs = defaultdict(list)
    for ins in insns:
        if ins.mnemonic not in ("call", "jmp", "mov", "lea"):
            continue
        for op in ins.operands:
            target = None
            if op.type == capstone.x86.X86_OP_MEM:
                mem = op.mem
                if is_64 and mem.base == capstone.x86.X86_REG_RIP:
                    target = ins.address + ins.size + mem.disp
                elif not is_64 and mem.base == 0:
                    target = mem.disp
            elif op.type == capstone.x86.X86_OP_IMM:
                target = op.imm
            if target in by_va:
                key = f'{by_va[target]["dll"]}!{by_va[target]["name"]}'
                xrefs[key].append(
                    {
                        "va": ins.address,
                        "mnemonic": ins.mnemonic,
                        "op_str": ins.op_str,
                    }
                )
    return dict(xrefs)


def find_near_constants(insns, xref_va, window=40):
    idx = None
    for i, ins in enumerate(insns):
        if ins.address == xref_va:
            idx = i
            break
    if idx is None:
        return []
    out = []
    for ins in insns[max(0, idx - window) : idx + 1]:
        vals = []
        for op in ins.operands:
            if op.type == capstone.x86.X86_OP_IMM:
                vals.append(op.imm)
            elif op.type == capstone.x86.X86_OP_MEM:
                if op.mem.disp:
                    vals.append(op.mem.disp)
        if vals:
            out.append({"va": ins.address, "text": f"{ins.mnemonic} {ins.op_str}", "values": vals})
    return out


def find_string_refs(pe, data, insns, wanted):
    image = pe.OPTIONAL_HEADER.ImageBase
    hits = []
    strings = list(ascii_strings(data))
    for s in strings:
        if not any(w.lower() in s["text"].lower() for w in wanted):
            continue
        rva = pe.get_rva_from_offset(s["offset"])
        va = image + rva
        refs = []
        for ins in insns:
            for op in ins.operands:
                target = None
                if op.type == capstone.x86.X86_OP_IMM:
                    target = op.imm
                elif op.type == capstone.x86.X86_OP_MEM:
                    mem = op.mem
                    if mem.base == capstone.x86.X86_REG_RIP:
                        target = ins.address + ins.size + mem.disp
                    elif mem.base == 0:
                        target = mem.disp
                if target == va:
                    refs.append({"va": ins.address, "text": f"{ins.mnemonic} {ins.op_str}"})
        hits.append({"string_va": va, "offset": s["offset"], "text": s["text"], "refs": refs})
    return hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--json-out")
    args = ap.parse_args()

    data = read_bytes(args.path)
    pe = pefile.PE(data=data)
    is_64 = pe.FILE_HEADER.Machine == pefile.MACHINE_TYPE["IMAGE_FILE_MACHINE_AMD64"]
    insns = disasm_text(pe, data, is_64)
    imps = imports(pe)
    exps = exports(pe)
    xrefs = find_import_xrefs(insns, imps, is_64)
    strings = [
        s
        for s in ascii_strings(data)
        if re.search(
            r"VID_|PID_|HidD|WriteFile|CreateFile|DirectInput|Force|Feedback|Effect|Constant|Sine|Axes|OEMForceFeedback|CLSID|Report|Vibration",
            s["text"],
            re.I,
        )
    ]

    call_context = {}
    for key, refs in xrefs.items():
        if any(name in key.lower() for name in ("hidd_setoutputreport", "writefile", "createfile")):
            call_context[key] = [
                {"xref": r, "near_constants": find_near_constants(insns, r["va"])}
                for r in refs
            ]

    result = {
        "path": args.path,
        "machine": "x64" if is_64 else "x86",
        "image_base": pe.OPTIONAL_HEADER.ImageBase,
        "exports": exps,
        "imports": imps,
        "import_xrefs": xrefs,
        "call_context": call_context,
        "interesting_strings": strings,
        "string_refs": find_string_refs(
            pe,
            data,
            insns,
            ["OEMForceFeedback", "VID_0079", "HidD_SetOutputReport", "Constant", "Sine Wave"],
        ),
    }
    text = json.dumps(result, indent=2)
    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as f:
            f.write(text)
    print(text[:20000])


if __name__ == "__main__":
    main()
