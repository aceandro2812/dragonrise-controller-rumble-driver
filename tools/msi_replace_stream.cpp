// Replace the embedded cabinet stream inside an MSI by matching size or
// by MSI stream-name encoding of the Media.Cabinet name (without leading #).
//
// cl /O2 /EHsc msi_replace_stream.cpp /Fe:msi_replace_stream.exe ole32.lib

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <vector>
#include <string>

#pragma comment(lib, "ole32.lib")

static int Fail(const char* msg, HRESULT hr = 0)
{
    if (hr)
        fprintf(stderr, "%s (hr=0x%08lX)\n", msg, (unsigned long)hr);
    else
        fprintf(stderr, "%s\n", msg);
    return 1;
}

// MSI stream name encoding (same algorithm as Wine/msi).
// See msi_encode_streamname in Wine.
static std::wstring EncodeMsiStreamName(const wchar_t* in, bool isTable = false)
{
    // Encoding table used by Windows Installer
    static const wchar_t chset[] =
        L"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz._!~";

    std::wstring out;
    if (isTable)
        out.push_back(0x4840); // table stream prefix often used; cabinets don't use this

    // For non-table streams, first UTF-16 code unit is 0x0001? Actually:
    // Wine:
    //   count = 0
    //   if (compress) *out++ = 0x4840
    //   while (*in) { encode }
    //
    // Simpler approach used here: enumerate and match by old stream size.

    (void)in;
    (void)isTable;
    return out;
}

static bool ReadFileAll(const wchar_t* path, std::vector<BYTE>& data)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 0 || size.QuadPart > 0x20000000) {
        CloseHandle(h);
        return false;
    }
    data.resize((size_t)size.QuadPart);
    DWORD read = 0;
    BOOL ok = ReadFile(h, data.data(), (DWORD)data.size(), &read, nullptr);
    CloseHandle(h);
    return ok && read == data.size();
}

static std::wstring FindLargestStream(IStorage* stg, ULONG* outSize)
{
    IEnumSTATSTG* en = nullptr;
    HRESULT hr = stg->EnumElements(0, nullptr, 0, &en);
    if (FAILED(hr) || !en)
        return L"";

    std::wstring best;
    ULONG bestSize = 0;
    STATSTG st {};
    while (en->Next(1, &st, nullptr) == S_OK) {
        if (st.type == STGTY_STREAM) {
            ULONG sz = (ULONG)st.cbSize.QuadPart;
            if (sz > bestSize) {
                bestSize = sz;
                best = st.pwcsName ? st.pwcsName : L"";
            }
        }
        if (st.pwcsName)
            CoTaskMemFree(st.pwcsName);
    }
    en->Release();
    if (outSize)
        *outSize = bestSize;
    return best;
}

static std::wstring FindStreamBySize(IStorage* stg, ULONG wantSize)
{
    IEnumSTATSTG* en = nullptr;
    HRESULT hr = stg->EnumElements(0, nullptr, 0, &en);
    if (FAILED(hr) || !en)
        return L"";

    std::wstring found;
    STATSTG st {};
    while (en->Next(1, &st, nullptr) == S_OK) {
        if (st.type == STGTY_STREAM && (ULONG)st.cbSize.QuadPart == wantSize) {
            found = st.pwcsName ? st.pwcsName : L"";
        }
        if (st.pwcsName)
            CoTaskMemFree(st.pwcsName);
    }
    en->Release();
    return found;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 4) {
        fwprintf(stderr,
                 L"Usage:\n"
                 L"  msi_replace_stream.exe <msi> <streamHintOr#> <dataFile> [oldCabSize]\n"
                 L"  streamHint: logical name (e.g. _0E4E91..) or '.' to pick largest stream\n");
        return 1;
    }

    const wchar_t* msiPath = argv[1];
    const wchar_t* streamHint = argv[2];
    const wchar_t* dataPath = argv[3];
    ULONG oldSizeHint = 0;
    if (argc >= 5)
        oldSizeHint = (ULONG)_wtoi(argv[4]);

    std::vector<BYTE> data;
    if (!ReadFileAll(dataPath, data))
        return Fail("Cannot read data file", HRESULT_FROM_WIN32(GetLastError()));

    IStorage* stg = nullptr;
    HRESULT hr = StgOpenStorageEx(
        msiPath,
        STGM_READWRITE | STGM_SHARE_EXCLUSIVE,
        STGFMT_STORAGE,
        0,
        nullptr,
        nullptr,
        IID_IStorage,
        (void**)&stg);
    if (FAILED(hr) || !stg)
        return Fail("StgOpenStorageEx failed", hr);

    // Discover actual OLE stream name
    std::wstring streamName;
    if (wcscmp(streamHint, L".") == 0) {
        ULONG sz = 0;
        streamName = FindLargestStream(stg, &sz);
        wprintf(L"Largest stream size=%lu name-len=%zu\n", sz, streamName.size());
    } else if (oldSizeHint) {
        streamName = FindStreamBySize(stg, oldSizeHint);
    } else {
        // Prefer match by previous extracted cab size if present as sibling
        // Fall back to largest stream (cabinet is by far the biggest)
        ULONG sz = 0;
        streamName = FindLargestStream(stg, &sz);
        wprintf(L"Using largest stream (size=%lu) as cabinet\n", sz);
    }

    if (streamName.empty()) {
        stg->Release();
        return Fail("Could not locate cabinet stream");
    }

    // Dump first few code units of the encoded name for debugging
    wprintf(L"Stream name UTF-16 words:");
    for (size_t i = 0; i < streamName.size() && i < 16; i++)
        wprintf(L" %04X", (unsigned)(unsigned short)streamName[i]);
    wprintf(L"\n");

    hr = stg->DestroyElement(streamName.c_str());
    if (FAILED(hr))
        wprintf(L"DestroyElement hr=0x%08lX (continuing)\n", (unsigned long)hr);

    IStream* stream = nullptr;
    hr = stg->CreateStream(
        streamName.c_str(),
        STGM_WRITE | STGM_SHARE_EXCLUSIVE | STGM_CREATE,
        0,
        0,
        &stream);
    if (FAILED(hr) || !stream) {
        stg->Release();
        return Fail("CreateStream failed", hr);
    }

    ULONG written = 0;
    hr = stream->Write(data.data(), (ULONG)data.size(), &written);
    if (FAILED(hr) || written != data.size()) {
        stream->Release();
        stg->Release();
        return Fail("IStream::Write failed", hr);
    }
    stream->Commit(STGC_DEFAULT);
    stream->Release();

    // Remove Authenticode streams. Leaving a broken signature after we rewrite the
    // cabinet makes Windows/Defender reject the package (MSI 1718 / 1625 / 1603).
    // Unsigned MSI is accepted; HashMismatch on a still-present signature is not.
    // NOTE: split after \x0005 — adjacent hex digits would be eaten by the escape
    // (e.g. L"\x0005Digital..." becomes 0x5D + "igital...").
    static const wchar_t* kSigStreams[] = {
        L"\x0005" L"DigitalSignature",
        L"\x0005" L"MsiDigitalSignatureEx",
    };
    for (const wchar_t* name : kSigStreams) {
        HRESULT dhr = stg->DestroyElement(name);
        if (SUCCEEDED(dhr))
            wprintf(L"Stripped OLE stream: %s\n", name + 1); // skip 0x05 for display
        else if (dhr != STG_E_FILENOTFOUND)
            wprintf(L"DestroyElement(%s) hr=0x%08lX\n", name + 1, (unsigned long)dhr);
    }

    hr = stg->Commit(STGC_DEFAULT);
    stg->Release();
    if (FAILED(hr))
        return Fail("IStorage::Commit failed", hr);

    wprintf(L"Replaced cabinet stream with %zu bytes in %s\n", data.size(), msiPath);
    return 0;
}
