#!/usr/bin/env python3
"""Replace a named stream inside an MSI (OLE compound file) using olefile-style
low-level structured storage via ctypes + Windows API."""
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import shutil
import sys
from pathlib import Path

ole32 = ctypes.windll.ole32
ole32.StgOpenStorageEx.argtypes = [
    wintypes.LPCWSTR,
    wintypes.DWORD,
    wintypes.DWORD,
    wintypes.DWORD,
    ctypes.c_void_p,
    ctypes.c_void_p,
    ctypes.POINTER(ctypes.c_void_p),
    ctypes.POINTER(ctypes.c_void_p),
]
ole32.StgOpenStorageEx.restype = ctypes.HRESULT

STGM_READWRITE = 0x00000002
STGM_SHARE_EXCLUSIVE = 0x00000010
STGM_CREATE = 0x00001000
STGTY_STREAM = 2
STGFMT_STORAGE = 0

# IStorage vtable indices (IUnknown 0-2, IStorage 3+)
# 3 CreateStream, 4 OpenStream, 5 CreateStorage, 6 OpenStorage,
# 7 CopyTo, 8 MoveElementTo, 9 Commit, 10 Revert, 11 EnumElements,
# 12 DestroyElement, ...


class IStorage(ctypes.Structure):
    pass


class IStream(ctypes.Structure):
    pass


def _vtable(obj):
    return ctypes.cast(ctypes.cast(obj, ctypes.POINTER(ctypes.c_void_p))[0], ctypes.POINTER(ctypes.c_void_p))


def stg_open(path: Path):
    stg = ctypes.c_void_p()
    hr = ole32.StgOpenStorageEx(
        str(path),
        STGM_READWRITE | STGM_SHARE_EXCLUSIVE,
        STGFMT_STORAGE,
        0,
        None,
        None,
        ctypes.byref(ctypes.c_void_p()),  # riid ignored when using NULL? actually need IID_IStorage
        ctypes.byref(stg),
    )
    # Proper call with IID_IStorage
    IID_IStorage = (ctypes.c_byte * 16)(
        0x0B, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46,
    )
    # Actually GUID layout is different - use from string
    return stg, hr


def guid_from_string(s: str):
    # {0000000b-0000-0000-C000-000000000046} IStorage
    import uuid

    u = uuid.UUID(s)
    return (ctypes.c_byte * 16).from_buffer_copy(u.bytes_le)


def open_storage(path: Path):
    iid = guid_from_string("0000000b-0000-0000-C000-000000000046")
    stg = ctypes.c_void_p()
    # StgOpenStorageEx(
    #   pwcsName, grfMode, stgfmt, grfAttrs, pStgOptions, reserved, riid, ppObjectOpen)
    hr = ole32.StgOpenStorageEx(
        str(path),
        STGM_READWRITE | STGM_SHARE_EXCLUSIVE,
        STGFMT_STORAGE,
        0,
        None,
        None,
        ctypes.byref(iid),
        ctypes.byref(stg),
    )
    if hr != 0:
        raise OSError(f"StgOpenStorageEx failed: 0x{hr & 0xFFFFFFFF:08X}")
    return stg


def call_method(obj, index, restype, *args):
    vt = _vtable(obj)
    func_type = ctypes.WINFUNCTYPE(restype, ctypes.c_void_p, *([type(a) for a in args]))
    # Better: define properly
    argtypes = [ctypes.c_void_p]
    for a in args:
        if isinstance(a, ctypes.c_void_p) or type(a).__name__.startswith("LP_") or isinstance(a, ctypes._SimpleCData):
            argtypes.append(type(a))
        elif isinstance(a, ctypes.Array):
            argtypes.append(ctypes.c_void_p)
        else:
            argtypes.append(type(a))
    # Simpler raw call
    fn = vt[index]
    # Use generic
    proto = ctypes.WINFUNCTYPE(restype, ctypes.c_void_p, *[_arg_type(a) for a in args])
    return proto(fn)(obj, *[_pass(a) for a in args])


def _arg_type(a):
    if isinstance(a, str):
        return wintypes.LPCWSTR
    if isinstance(a, int):
        return wintypes.DWORD
    if isinstance(a, ctypes.c_void_p):
        return ctypes.c_void_p
    if isinstance(a, ctypes._Pointer):
        return type(a)
    if isinstance(a, ctypes.Array):
        return ctypes.c_void_p
    return ctypes.c_void_p


def _pass(a):
    if isinstance(a, str):
        return a
    if isinstance(a, ctypes.Array):
        return ctypes.byref(a)
    return a


def destroy_element(stg, name: str):
    # IStorage::DestroyElement is vtable index 12
    vt = _vtable(stg)
    proto = ctypes.WINFUNCTYPE(ctypes.HRESULT, ctypes.c_void_p, wintypes.LPCWSTR)
    hr = proto(vt[12])(stg, name)
    return hr


def create_stream(stg, name: str):
    # CreateStream index 3
    vt = _vtable(stg)
    stream = ctypes.c_void_p()
    # HRESULT CreateStream(name, grfMode, reserved1, reserved2, ppstm)
    proto = ctypes.WINFUNCTYPE(
        ctypes.HRESULT,
        ctypes.c_void_p,
        wintypes.LPCWSTR,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.DWORD,
        ctypes.POINTER(ctypes.c_void_p),
    )
    hr = proto(vt[3])(
        stg,
        name,
        STGM_READWRITE | STGM_SHARE_EXCLUSIVE | STGM_CREATE,
        0,
        0,
        ctypes.byref(stream),
    )
    if hr != 0:
        raise OSError(f"CreateStream failed: 0x{hr & 0xFFFFFFFF:08X}")
    return stream


def stream_write(stream, data: bytes):
    # IStream::Write is vtable index 4 (IUnknown 0-2, Read=3, Write=4)
    vt = _vtable(stream)
    written = wintypes.ULONG()
    buf = (ctypes.c_char * len(data)).from_buffer_copy(data)
    proto = ctypes.WINFUNCTYPE(
        ctypes.HRESULT,
        ctypes.c_void_p,
        ctypes.c_void_p,
        wintypes.ULONG,
        ctypes.POINTER(wintypes.ULONG),
    )
    hr = proto(vt[4])(stream, buf, len(data), ctypes.byref(written))
    if hr != 0:
        raise OSError(f"IStream::Write failed: 0x{hr & 0xFFFFFFFF:08X}")
    return written.value


def stream_commit(stream):
    vt = _vtable(stream)
    proto = ctypes.WINFUNCTYPE(ctypes.HRESULT, ctypes.c_void_p, wintypes.DWORD)
    return proto(vt[8])(stream, 0)  # Commit index 8 for IStream


def stream_release(stream):
    vt = _vtable(stream)
    proto = ctypes.WINFUNCTYPE(wintypes.ULONG, ctypes.c_void_p)
    return proto(vt[2])(stream)


def storage_commit(stg):
    vt = _vtable(stg)
    # IStorage::Commit index 9
    proto = ctypes.WINFUNCTYPE(ctypes.HRESULT, ctypes.c_void_p, wintypes.DWORD)
    hr = proto(vt[9])(stg, 0)
    if hr != 0:
        raise OSError(f"IStorage::Commit failed: 0x{hr & 0xFFFFFFFF:08X}")


def storage_release(stg):
    vt = _vtable(stg)
    proto = ctypes.WINFUNCTYPE(wintypes.ULONG, ctypes.c_void_p)
    return proto(vt[2])(stg)


def replace_stream(msi: Path, stream_name: str, data: bytes) -> None:
    stg = open_storage(msi)
    try:
        hr = destroy_element(stg, stream_name)
        # S_OK or STG_E_FILENOTFOUND is fine
        stream = create_stream(stg, stream_name)
        try:
            stream_write(stream, data)
            stream_commit(stream)
        finally:
            stream_release(stream)
        storage_commit(stg)
    finally:
        storage_release(stg)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("msi", type=Path)
    ap.add_argument("stream_name")
    ap.add_argument("data_file", type=Path)
    args = ap.parse_args()
    data = args.data_file.read_bytes()
    replace_stream(args.msi, args.stream_name, data)
    print(f"Replaced stream {args.stream_name} ({len(data)} bytes) in {args.msi}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
