#include <Memory/DmaBackend.h>
#include <Memory/Memory.h>
#include <Core/Data.h>
#include <Core/Process.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <stdexcept>

FGameData GameData;
static const std::string ProcessName = "WardogsClient-Win64-Shipping.exe";
static void Check(bool condition, const char* description)
{
    if (!condition) throw std::runtime_error(description);
}
static void Mode(const char* mode) { SetEnvironmentVariableA("UEDUMPER_TEST_MODE", mode); }

int main()
{
    HMODULE fixture = LoadLibraryW(L"vmm.dll");
    if (!fixture) return 1;
    const auto handles = reinterpret_cast<unsigned(*)()>(GetProcAddress(fixture, "TestHandles"));
    const auto allocations = reinterpret_cast<unsigned(*)()>(GetProcAddress(fixture, "TestAllocations"));
    const auto reads = reinterpret_cast<unsigned(*)()>(GetProcAddress(fixture, "TestReads"));
    const auto flags = reinterpret_cast<ULONG64(*)()>(GetProcAddress(fixture, "TestFlags"));
    if (!handles || !allocations || !reads || !flags) return 1;
    try {
        DmaOptions options;
        options.Device = "fixture";
        {
            DmaBackend backend(options);
            for (const char* failure : { "init_fail", "refresh_fail", "enumeration_fail", "no_process",
                                       "ambiguous", "module_fail", "zero_size" }) {
                Mode(failure);
                Check(!backend.Attach(ProcessName), failure);
                Check(handles() == 0 && allocations() == 0, "failed attach leaked resources");
            }
            Mode("normal");
            Check(backend.Attach(ProcessName), "full-name process matching");
            Check(backend.GetModuleBase(ProcessName) == 0x140000000ULL, "module base");
            Check(backend.GetModuleSize(ProcessName) == 0x2000000, "module size");
            Check(allocations() == 0 && handles() == 1, "module/process allocations freed");
            uint64_t value = 0;
            Check(backend.ReadRaw(0x500000000, &value, sizeof(value)) && value == 0x5a5a5a5a5a5a5a5aULL,
                "DMA read");
            Check(flags() == 1, "DMA read bypasses data cache");
            Mode("stale_translation");
            Check(!backend.ReadRaw(0x500000000, &value, sizeof(value)), "stale translation fails initial read");
            Check(backend.RefreshTranslationCache(), "DMA translation cache refresh");
            Check(backend.ReadRaw(0x500000000, &value, sizeof(value)) && value == 0x5a5a5a5a5a5a5a5aULL,
                "translation refresh recovers DMA read");
            Mode("refresh_tlb_fail");
            Check(!backend.RefreshTranslationCache(), "translation refresh failure propagates");
            for (const char* failure : { "partial", "read_fail" }) {
                Mode(failure);
                value = 123;
                Check(!backend.ReadRaw(0x500000000, &value, sizeof(value)) && !value,
                    "partial reads are zeroed and rejected");
            }
            const auto count = reads();
            Check(!backend.ReadRaw(0x1000, &value, 0x100000000ULL), "DWORD length overflow");
            Check(!backend.ReadRaw((std::numeric_limits<uintptr_t>::max)() - 2, &value, sizeof(value)),
                "address overflow");
            Check(reads() == count, "invalid reads reached the DLL");
            backend.Shutdown();
            backend.Shutdown();
            Check(handles() == 0, "idempotent shutdown");
        }
        Mode("ambiguous");
        options.ProcessId = 42;
        {
            DmaBackend backend(options);
            Check(backend.Attach(ProcessName), "explicit target PID selection");
        }
        options.ProcessId = 0;
        Memory::RegisterBackend("dma", [options] { return new DmaBackend(options); });
        Mode("normal");
        {
            Memory memory;
            Check(memory.Init(ProcessName), "DMA is the default backend");
            Mode("partial");
            std::array<uint8_t, 32> bytes{};
            Check(!memory.ReadUseCache(0x500000ff8, bytes.data(), bytes.size()), "failed page read");
            Check(std::all_of(bytes.begin(), bytes.end(), [](auto b) { return b == 0; }), "failed read clears buffer");
            Mode("normal");
            Check(memory.ReadUseCache(0x500000ff8, bytes.data(), bytes.size()), "failed page is retried");
            Check(std::all_of(bytes.begin(), bytes.end(), [](auto b) { return b == 0x5a; }), "cross-page cache data");
            const auto count = reads();
            Check(memory.ReadUseCache(0x500000ff8, bytes.data(), bytes.size()), "cached read");
            Check(reads() == count, "successful pages are cached");
            Check(memory.RefreshTranslationCache(), "facade refreshes backend translation cache");
            Mode("partial");
            Check(!memory.ReadUseCache(0x500000ff8, bytes.data(), bytes.size()), "translation refresh invalidates local page cache");
            Mode("normal");
            Check(memory.Init(ProcessName) && handles() == 1, "reattach releases previous handle");
            Check(memory.ReadUseCache(0x500000ff8, bytes.data(), bytes.size()), "reattach clears cache");
            Check(!memory.Init(ProcessName, "unknown") && handles() == 0, "unknown backend clears previous state");
            Check(memory.GetBaseDaddy(ProcessName) == 0, "failed init clears base");
            Check(memory.Init(ProcessName, ""), "empty backend selects DMA");
            Mode("hole");
            std::vector<uint8_t> module(17 * 1024 * 1024);
            Check(memory.Read(0x500000000, module.data(), module.size()), "sparse large module copy");
            for (size_t i = 0; i < module.size(); ++i) {
                if (module[i] != (i >= 0x2000 && i < 0x3000 ? 0 : 0x5a))
                    throw std::runtime_error("sparse module lost readable pages");
            }
            Mode("read_fail");
            Check(!memory.Read(0x500000000, module.data(), module.size()), "unreadable module must fail");
            Check(std::all_of(module.begin(), module.end(), [](auto b) { return b == 0; }), "failed module is zeroed");
        }
        Check(handles() == 0 && allocations() == 0, "Memory destructor releases DMA resources");
        Mode("normal");
        Check(mem.Init(ProcessName), "Process test setup");
        GameData.Global.Base = mem.GetBaseDaddy(ProcessName);
        GameData.Global.Size = mem.GetBaseSize(ProcessName);
        Check(Process::Init() && GameData.MemorySize == 0x2000000, "module copy uses map size without PE headers");
        Mode("read_fail");
        Check(!Process::Init() && !GameData.Memory && !GameData.MemorySize, "Process propagates DMA read failure");
        mem.Shutdown();
        Check(handles() == 0 && allocations() == 0, "all resources released");
        {
            Memory local;
            Check(local.Init("MemoryTests.exe", "rpm"), "RPM backend still attaches locally");
            const auto dos = local.Read<IMAGE_DOS_HEADER>(local.GetBaseDaddy("MemoryTests.exe"));
            Check(dos.e_magic == IMAGE_DOS_SIGNATURE, "RPM reads actual process memory");
        }
        Mode(nullptr);
        std::puts("PASS: DMA lifecycle, process selection, reads, cache and sparse module regression tests");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        Mode(nullptr);
        return 1;
    }
    FreeLibrary(fixture);
    return 0;
}
