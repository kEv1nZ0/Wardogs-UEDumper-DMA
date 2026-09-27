#include "RpmBackend.h"

#include <cstdio>
#include <cstring>
#include <tlhelp32.h>

// ---- helpers ---------------------------------------------------------------

// Case-insensitive compare of a wide string against an ANSI image name.
static bool WEqualsA(const wchar_t* wide, const std::string& ansi)
{
    char buf[256] = {};
    WideCharToMultiByte(CP_ACP, 0, wide, -1, buf, sizeof(buf), nullptr, nullptr);
    return _stricmp(buf, ansi.c_str()) == 0;
}

static DWORD FindPidByImageName(const std::string& image)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);

    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (WEqualsA(pe.szExeFile, image)) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static bool FindModule(DWORD pid, const std::string& moduleName, uintptr_t* base, size_t* size)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;

    MODULEENTRY32W me = {};
    me.dwSize = sizeof(me);

    bool found = false;
    if (Module32FirstW(snap, &me)) {
        do {
            if (WEqualsA(me.szModule, moduleName)) {
                if (base) *base = reinterpret_cast<uintptr_t>(me.modBaseAddr);
                if (size) *size = static_cast<size_t>(me.modBaseSize);
                found = true;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

// ---- IBackend --------------------------------------------------------------

bool RpmBackend::Attach(const std::string& processName)
{
    Shutdown();

    ProcessId = FindPidByImageName(processName);
    if (!ProcessId) {
        std::printf("[rpm] process not found: %s\n", processName.c_str());
        return false;
    }

    ProcessHandle = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, ProcessId);
    if (!ProcessHandle) {
        std::printf("[rpm] OpenProcess failed (pid=%lu, err=%lu) - protected process or insufficient rights\n",
                    ProcessId, GetLastError());
        return false;
    }

    uintptr_t base = 0;
    if (!FindModule(ProcessId, processName, &base, nullptr) || !base) {
        std::printf("[rpm] module not found: %s\n", processName.c_str());
        Shutdown();
        return false;
    }

    std::printf("[rpm] attached pid=%lu base=0x%llX\n", ProcessId, (unsigned long long)base);
    return true;
}

void RpmBackend::Shutdown()
{
    if (ProcessHandle) {
        CloseHandle(ProcessHandle);
        ProcessHandle = nullptr;
    }
    ProcessId = 0;
}

bool RpmBackend::ReadRaw(uintptr_t address, void* buffer, size_t size)
{
    if (!ProcessHandle) return false;

    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(ProcessHandle, reinterpret_cast<LPCVOID>(address), buffer, size, &bytesRead))
        return false;

    return bytesRead == size;
}

uintptr_t RpmBackend::GetModuleBase(const std::string& moduleName)
{
    uintptr_t base = 0;
    if (!FindModule(ProcessId, moduleName, &base, nullptr)) return 0;
    return base;
}

size_t RpmBackend::GetModuleSize(const std::string& moduleName)
{
    size_t size = 0;
    if (!FindModule(ProcessId, moduleName, nullptr, &size)) return 0;
    return size;
}
