#pragma once

#include <string>
#include <windows.h>

#include <Memory/IBackend.h>

// =============================================================================
// RpmBackend - reference IBackend implementation on top of ReadProcessMemory.
//
// Works out of the box on unprotected processes (own user session, no
// anti-cheat blocking handle access). It is intentionally simple: use it as a
// template when writing a driver-backed backend.
// =============================================================================

class RpmBackend : public IBackend
{
public:
    ~RpmBackend() override { Shutdown(); }

    const char* GetName() const override { return "rpm"; }

    bool Attach(const std::string& processName) override;
    void Shutdown() override;

    bool ReadRaw(uintptr_t address, void* buffer, size_t size) override;
    uintptr_t GetModuleBase(const std::string& moduleName) override;
    size_t GetModuleSize(const std::string& moduleName) override;

private:
    HANDLE ProcessHandle = nullptr;
    DWORD ProcessId = 0;
};
