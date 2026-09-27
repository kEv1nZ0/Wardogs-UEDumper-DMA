#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// =============================================================================
// IBackend - memory backend interface.
//
// Implement this interface to plug a custom read primitive (kernel driver,
// DMA hardware, hypervisor, debugger API, ...) into the dumper.
// See Memory/RpmBackend.h for a complete reference implementation and
// Memory::RegisterBackend() for how to activate a custom backend from code.
//
// Contract:
//   - Attach() must locate the target process and succeed before any Read.
//   - ReadRaw() returns false on ANY failure (partial reads count as failure
//     and may leave the buffer in an undefined state).
//   - GetModuleBase/GetModuleSize operate on the attached process.
//   - All methods are called from a single thread (no internal locking needed).
// =============================================================================

class IBackend
{
public:
    virtual ~IBackend() = default;

    // Human-readable backend name, used in log output.
    virtual const char* GetName() const = 0;

    // Attach to the target process (e.g. "WardogsClient-Win64-Shipping.exe").
    virtual bool Attach(const std::string& processName) = 0;

    // Release the handle / unload resources.
    virtual void Shutdown() = 0;

    // Raw virtual memory read of the attached process.
    virtual bool ReadRaw(uintptr_t address, void* buffer, size_t size) = 0;

    // Optional recovery for backends that cache virtual-to-physical mappings.
    // False means unsupported or failed; callers may still retry the read.
    virtual bool RefreshTranslationCache() { return false; }

    // Base address of a loaded module inside the attached process.
    virtual uintptr_t GetModuleBase(const std::string& moduleName) = 0;

    // Size of the module image (SizeOfImage), 0 on failure.
    virtual size_t GetModuleSize(const std::string& moduleName) = 0;
};
