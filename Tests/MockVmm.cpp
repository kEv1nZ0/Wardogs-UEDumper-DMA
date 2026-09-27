// Deterministic MemProcFS API fixture. Built only into x64/dma-tests/vmm.dll.
#pragma warning(disable: 4200)
#include <ThirdParty/MemProcFS/include/vmmdll.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <string>

static unsigned Handles = 0, Allocations = 0, Reads = 0;
static ULONG64 Flags = 0;
static bool TranslationFresh = false;
static std::map<ULONG64, std::array<BYTE, 4096>> Pages;
static constexpr ULONG64 Base = 0x140000000;
static constexpr ULONG64 Table = 0x2000000000;
static constexpr ULONG64 Items = 0x2000010000;
static constexpr ULONG64 Objects = 0x2000020000;
static constexpr ULONG64 Names = 0x3000000000;

static bool Mode(const char* value)
{
    char mode[64]{};
    GetEnvironmentVariableA("UEDUMPER_TEST_MODE", mode, sizeof(mode));
    return std::strcmp(mode, value) == 0;
}

static void Put(ULONG64 address, const void* data, size_t size)
{
    const auto* bytes = static_cast<const BYTE*>(data);
    for (size_t i = 0; i < size; ++i) Pages[(address + i) & ~0xfffULL][(address + i) & 0xfff] = bytes[i];
}

template <typename T> static void Put(ULONG64 address, T value) { Put(address, &value, sizeof(value)); }

extern "C" VMM_HANDLE VMMDLL_Initialize(DWORD argc, LPCSTR argv[])
{
    if (Mode("init_fail") || argc < 3 || argv[0][0] || std::strcmp(argv[1], "-device")) return nullptr;
    Pages.clear();
    TranslationFresh = false;
    Put(Base + 0x1000, Table);
    Put<uint32_t>(Base + 0x1014, 10);
    Put<uint32_t>(Base + 0x101c, 1);
    // The live reader may tag chunk pointers in their low four bits.
    Put(Table, Items | 0x5);
    // Match the live pool: +0x10 is null, the first block is at +0x20.
    Put(Base + 0x2020, Names);
    const char* names[] = { "/Script/CoreUObject", "Object", "Field", "Struct", "Class",
        "Function", "Enum", "Package", "Property", "Actor" };
    for (unsigned i = 0; i < 10; ++i) {
        Put<uint64_t>(Items + i * 0x18 + 8, 0x4000000000000000ULL);
        Put(Items + i * 0x18 + 0x10, Objects + i * 0x100);
        Put<uint32_t>(Objects + i * 0x100 + 0x18, i * 0x80);
        Put<uint16_t>(Names + i * 0x400 + 8, static_cast<uint16_t>(std::strlen(names[i]) << 6));
        Put(Names + i * 0x400 + 12, names[i], std::strlen(names[i]));
    }
    ++Handles;
    return reinterpret_cast<VMM_HANDLE>(0x1234);
}

extern "C" VOID VMMDLL_Close(VMM_HANDLE) { --Handles; }
extern "C" BOOL VMMDLL_ConfigSet(VMM_HANDLE, ULONG64 option, ULONG64)
{
    if (option == VMMDLL_OPT_REFRESH_FREQ_TLB) {
        if (Mode("refresh_tlb_fail")) return FALSE;
        TranslationFresh = true;
        return TRUE;
    }
    return option == VMMDLL_OPT_REFRESH_ALL && !Mode("refresh_fail");
}

extern "C" BOOL VMMDLL_ProcessGetInformationAll(
    VMM_HANDLE, PVMMDLL_PROCESS_INFORMATION* output, PDWORD count)
{
    if (Mode("enumeration_fail")) return FALSE;
    *count = 3;
    *output = static_cast<PVMMDLL_PROCESS_INFORMATION>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
        *count * sizeof(VMMDLL_PROCESS_INFORMATION)));
    if (!*output) return FALSE;
    ++Allocations;
    for (DWORD i = 0; i < *count; ++i) {
        (*output)[i].dwPID = 41 + i;
        strcpy_s((*output)[i].szName, "WardogsClient-W");
        strcpy_s((*output)[i].szNameLong, "WardogsClient-Win64-Shipping.exe");
    }
    strcpy_s((*output)[0].szNameLong, "WardogsClient-Win64-Other.exe");
    if (Mode("no_process")) strcpy_s((*output)[1].szNameLong, "Other.exe");
    (*output)[2].dwState = Mode("ambiguous") ? 0 : 1;
    return TRUE;
}

extern "C" BOOL VMMDLL_Map_GetModuleFromNameU(
    VMM_HANDLE, DWORD pid, LPCSTR name, PVMMDLL_MAP_MODULEENTRY* output, DWORD)
{
    if (Mode("module_fail") || pid != 42 || _stricmp(name, "WardogsClient-Win64-Shipping.exe")) return FALSE;
    *output = static_cast<PVMMDLL_MAP_MODULEENTRY>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
        sizeof(VMMDLL_MAP_MODULEENTRY)));
    if (!*output) return FALSE;
    ++Allocations;
    (*output)->vaBase = Base;
    (*output)->cbImageSize = Mode("zero_size") ? 0 : 0x2000000;
    return TRUE;
}

extern "C" VOID VMMDLL_MemFree(PVOID allocation)
{
    if (allocation) { --Allocations; HeapFree(GetProcessHeap(), 0, allocation); }
}

extern "C" BOOL VMMDLL_MemReadEx(
    VMM_HANDLE, DWORD pid, ULONG64 address, PBYTE output, DWORD size, PDWORD bytesRead, ULONG64 flags)
{
    ++Reads;
    Flags = flags;
    *bytesRead = 0;
    if (pid != 42) return FALSE;
    if (Mode("partial") || Mode("read_fail") ||
        (Mode("stale_translation") && !TranslationFresh) ||
        (Mode("hole") && address < 0x500003000ULL && address + size > 0x500002000ULL)) {
        std::memset(output, 0xcc, size);
        *bytesRead = size - 1;
        return !Mode("read_fail");
    }
    std::memset(output, 0x5a, size);
    for (const auto& [page, data] : Pages) {
        const auto start = (std::max)(address, page);
        const auto end = (std::min)(address + size, page + 4096);
        if (start < end) std::memcpy(output + start - address, data.data() + start - page,
            static_cast<size_t>(end - start));
    }
    *bytesRead = size;
    return TRUE;
}

extern "C" __declspec(dllexport) unsigned TestHandles() { return Handles; }
extern "C" __declspec(dllexport) unsigned TestAllocations() { return Allocations; }
extern "C" __declspec(dllexport) unsigned TestReads() { return Reads; }
extern "C" __declspec(dllexport) ULONG64 TestFlags() { return Flags; }
