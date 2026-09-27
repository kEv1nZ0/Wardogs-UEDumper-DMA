#include "DmaBackend.h"

#pragma warning(push)
#pragma warning(disable: 4200) // Flexible array members in the vendor C headers.
#include <ThirdParty/MemProcFS/include/vmmdll.h>
#pragma warning(pop)
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <utility>
#include <vector>

struct DmaBackend::Runtime
{
    HMODULE Ftd = nullptr;
    HMODULE LeechCore = nullptr;
    HMODULE Vmm = nullptr;
    VMM_HANDLE Handle = nullptr;

    decltype(&VMMDLL_Initialize) Initialize = nullptr;
    decltype(&VMMDLL_Close) Close = nullptr;
    decltype(&VMMDLL_ConfigSet) ConfigSet = nullptr;
    decltype(&VMMDLL_ProcessGetInformationAll) ProcessGetInformationAll = nullptr;
    decltype(&VMMDLL_Map_GetModuleFromNameU) Map_GetModuleFromNameU = nullptr;
    decltype(&VMMDLL_MemFree) MemFree = nullptr;
    decltype(&VMMDLL_MemReadEx) MemReadEx = nullptr;

    ~Runtime()
    {
        if (Handle && Close) Close(Handle);
        if (Vmm) FreeLibrary(Vmm);
        if (LeechCore) FreeLibrary(LeechCore);
        if (Ftd) FreeLibrary(Ftd);
    }

    template <typename T>
    bool Resolve(T& function, const char* name)
    {
        function = reinterpret_cast<T>(GetProcAddress(Vmm, name));
        if (!function) std::printf("[dma] vmm.dll is missing export %s\n", name);
        return function != nullptr;
    }
};

static std::filesystem::path ExecutableDirectory()
{
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) return {};
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}

static HMODULE LoadRuntimeDll(const std::filesystem::path& directory, const wchar_t* name)
{
    const auto path = directory / name;
    HMODULE module = LoadLibraryExW(path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module) {
        const DWORD error = GetLastError();
        std::printf("[dma] cannot load %ls (Windows error %lu).\n", path.c_str(), error);
        if (!std::filesystem::exists(path)) {
            std::printf("      The file is not present. Fetch the DMA runtime with\n"
                        "      scripts/Fetch-MemProcFS.ps1.\n");
        } else if (_wcsicmp(name, L"FTD3XX.dll") == 0 || _wcsicmp(name, L"leechcore.dll") == 0) {
            // leechcore.dll imports FTD3XX.dll directly, so a missing FTD3XX.dll
            // makes leechcore.dll fail to load with the same error code - for
            // every device, not only the PCILeech FPGA.
            std::printf("      A required dependency is missing. leechcore.dll imports FTD3XX.dll,\n"
                        "      so the vendor FTD3XX.dll must sit next to UEDumper.exe for any device\n"
                        "      (https://ftdichip.com/drivers/d3xx-drivers/). Copy it into\n"
                        "      ThirdParty\\MemProcFS\\bin\\ next to leechcore.dll.\n");
        } else {
            std::printf("      The file exists but its dependencies could not be resolved.\n"
                        "      Keep the matching x64 vmm.dll, leechcore.dll and FTD3XX.dll together.\n");
        }
    }
    return module;
}

DmaBackend::DmaBackend(DmaOptions options) : Options(std::move(options)) {}
DmaBackend::~DmaBackend() { Shutdown(); }

bool DmaBackend::Attach(const std::string& processName)
{
    Shutdown();
    if (processName.empty() || Options.Device.empty()) return false;
    const auto directory = ExecutableDirectory();
    if (directory.empty()) return false;

    // Keep startup resources local until every initialization step succeeds.
    auto runtime = std::make_unique<Runtime>();
    if (Options.Remote.empty() && Options.Device.compare(0, 4, "fpga") == 0) {
        runtime->Ftd = LoadRuntimeDll(directory, L"FTD3XX.dll");
        if (!runtime->Ftd) return false;
    }
    runtime->LeechCore = LoadRuntimeDll(directory, L"leechcore.dll");
    if (!runtime->LeechCore) return false;
    runtime->Vmm = LoadRuntimeDll(directory, L"vmm.dll");
    if (!runtime->Vmm) return false;

#define RESOLVE_VMM(name) if (!runtime->Resolve(runtime->name, "VMMDLL_" #name)) return false
    RESOLVE_VMM(Initialize);
    RESOLVE_VMM(Close);
    RESOLVE_VMM(ConfigSet);
    RESOLVE_VMM(ProcessGetInformationAll);
    RESOLVE_VMM(Map_GetModuleFromNameU);
    RESOLVE_VMM(MemFree);
    RESOLVE_VMM(MemReadEx);
#undef RESOLVE_VMM

    // VMM's API examples and the reference project use an empty argv[0].
    std::vector<std::string> arguments = { "", "-device", Options.Device };
    if (!Options.Remote.empty()) {
        arguments.emplace_back("-remote");
        arguments.push_back(Options.Remote);
    }
    if (!Options.MemMap.empty() && _stricmp(Options.MemMap.c_str(), "none") != 0) {
        std::string map = Options.MemMap;
        if (_stricmp(map.c_str(), "auto") == 0) {
            map = "auto";
        } else {
            auto path = std::filesystem::path(std::u8string(map.begin(), map.end()));
            if (path.is_relative()) path = directory / path;
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error)) {
                std::printf("[dma] DMA.MemMap file does not exist: %s\n", map.c_str());
                return false;
            }
            const auto utf8 = path.u8string();
            map.assign(utf8.begin(), utf8.end());
        }
        arguments.emplace_back("-memmap");
        arguments.push_back(std::move(map));
    }
    if (Options.Debug) {
        arguments.emplace_back("-printf");
        arguments.emplace_back("-v");
    }
    std::vector<LPCSTR> argv;
    for (const auto& argument : arguments) argv.push_back(argument.c_str());

    std::printf("[dma] initializing device %s ...\n", Options.Device.c_str());
    runtime->Handle = runtime->Initialize(static_cast<DWORD>(argv.size()), argv.data());
    if (!runtime->Handle) {
        std::printf("[dma] initialization failed. Check the device connection, target power,\n"
                    "      and whether another application owns the DMA device.\n"
                    "      Set DMA.Debug=1 in config.ini for MemProcFS diagnostics.\n");
        return false;
    }
    if (!runtime->ConfigSet(runtime->Handle, VMMDLL_OPT_REFRESH_ALL, 0)) {
        std::printf("[dma] failed to refresh the target process and memory maps\n");
        return false;
    }

    // Match the full image name, not the 15-character kernel process name.
    PVMMDLL_PROCESS_INFORMATION processes = nullptr;
    DWORD count = 0;
    if (!runtime->ProcessGetInformationAll(runtime->Handle, &processes, &count) || !processes) {
        if (processes) runtime->MemFree(processes);
        std::printf("[dma] cannot enumerate processes on the target machine\n");
        return false;
    }
    DWORD pid = 0;
    unsigned matches = 0;
    for (DWORD i = 0; i < count; ++i) {
        const auto& process = processes[i];
        if (process.dwState != 0 || !process.dwPID) continue;
        if (Options.ProcessId && process.dwPID != Options.ProcessId) continue;
        if (_strnicmp(process.szNameLong, processName.c_str(), sizeof(process.szNameLong)) != 0) continue;
        pid = process.dwPID;
        ++matches;
    }
    runtime->MemFree(processes);
    if (matches != 1) {
        std::printf("[dma] found %u active processes named %s on the target machine.\n",
                    matches, processName.c_str());
        if (matches > 1) std::printf("      Set DMA.Pid in config.ini to select one.\n");
        return false;
    }

    Dll = std::move(runtime);
    ProcessId = pid;
    if (!FindModule(processName, MainBase, MainSize)) {
        std::printf("[dma] cannot resolve the target module: %s (pid=%lu)\n", processName.c_str(), pid);
        Shutdown();
        return false;
    }
    MainModule = processName;
    std::printf("[dma] attached to target pid=%lu module=%s base=0x%llX size=0x%llX\n",
                pid, processName.c_str(), (unsigned long long)MainBase, (unsigned long long)MainSize);
    return true;
}

void DmaBackend::Shutdown()
{
    Dll.reset();
    ProcessId = 0;
    MainModule.clear();
    MainBase = 0;
    MainSize = 0;
}

bool DmaBackend::ReadRaw(uintptr_t address, void* buffer, size_t size)
{
    if (!Dll || !Dll->Handle || !ProcessId || !buffer || !size ||
        size > (std::numeric_limits<DWORD>::max)() ||
        address > (std::numeric_limits<uintptr_t>::max)() - (size - 1)) return false;

    DWORD bytesRead = 0;
    const bool success = Dll->MemReadEx(Dll->Handle, ProcessId, address,
        static_cast<PBYTE>(buffer), static_cast<DWORD>(size), &bytesRead, VMMDLL_FLAG_NOCACHE) != FALSE;
    if (!success || bytesRead != size) {
        if (Options.Debug) {
            std::printf("[dma] read failed: va=0x%llX requested=%zu received=%lu api_success=%u\n",
                static_cast<unsigned long long>(address), size, bytesRead, unsigned(success));
        }
        // A partial pointer must never reach the UE parser or its page cache.
        std::memset(buffer, 0, size);
        return false;
    }
    return true;
}

bool DmaBackend::RefreshTranslationCache()
{
    // NOCACHE bypasses data caching, but it does not flush cached page tables.
    return Dll && Dll->Handle &&
        Dll->ConfigSet(Dll->Handle, VMMDLL_OPT_REFRESH_FREQ_TLB, 0) != FALSE;
}

bool DmaBackend::FindModule(const std::string& name, uintptr_t& base, size_t& size)
{
    base = 0;
    size = 0;
    if (!Dll || !ProcessId) return false;
    PVMMDLL_MAP_MODULEENTRY entry = nullptr;
    const bool success = Dll->Map_GetModuleFromNameU(Dll->Handle, ProcessId,
        name.c_str(), &entry, VMMDLL_MODULE_FLAG_NORMAL) != FALSE;
    if (success && entry && !entry->fWoW64) {
        base = static_cast<uintptr_t>(entry->vaBase);
        size = entry->cbImageSize;
    }
    if (entry) Dll->MemFree(entry);
    return base != 0 && size != 0;
}

uintptr_t DmaBackend::GetModuleBase(const std::string& moduleName)
{
    if (_stricmp(moduleName.c_str(), MainModule.c_str()) == 0) return MainBase;
    uintptr_t base = 0;
    size_t size = 0;
    FindModule(moduleName, base, size);
    return base;
}

size_t DmaBackend::GetModuleSize(const std::string& moduleName)
{
    if (_stricmp(moduleName.c_str(), MainModule.c_str()) == 0) return MainSize;
    uintptr_t base = 0;
    size_t size = 0;
    FindModule(moduleName, base, size);
    return size;
}
