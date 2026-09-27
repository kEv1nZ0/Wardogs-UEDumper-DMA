#pragma once

#include <memory>
#include <Memory/IBackend.h>

struct DmaOptions
{
    std::string Device = "fpga://algo=0";
    std::string Remote;
    std::string MemMap;
    uint32_t ProcessId = 0;
    bool Debug = false;
};

// MemProcFS reads virtual memory on the DMA target machine. DLLs are loaded
// next to the executable, so selecting the RPM backend needs no DMA runtime.
class DmaBackend final : public IBackend
{
public:
    explicit DmaBackend(DmaOptions options = {});
    ~DmaBackend() override;

    const char* GetName() const override { return "dma"; }
    bool Attach(const std::string& processName) override;
    void Shutdown() override;
    bool ReadRaw(uintptr_t address, void* buffer, size_t size) override;
    bool RefreshTranslationCache() override;
    uintptr_t GetModuleBase(const std::string& moduleName) override;
    size_t GetModuleSize(const std::string& moduleName) override;

private:
    struct Runtime;
    bool FindModule(const std::string& name, uintptr_t& base, size_t& size);

    DmaOptions Options;
    std::unique_ptr<Runtime> Dll;
    uint32_t ProcessId = 0;
    std::string MainModule;
    uintptr_t MainBase = 0;
    size_t MainSize = 0;
};
