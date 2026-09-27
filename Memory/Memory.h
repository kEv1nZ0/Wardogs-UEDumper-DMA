#pragma once

// =============================================================================
// Memory - facade over a pluggable IBackend.
//
// Keeps the small API the dumper core relies on (mem.Read / mem.ReadUseCache /
// mem.GetBaseDaddy / ...) while delegating the actual read primitive to a
// backend selected at startup:
//
//     Memory::Init("Game-Win64-Shipping.exe", "dma");   // built-in default
//
// Custom backends (kernel driver, DMA, ...) hook in via RegisterBackend()
// before Init, or by passing their name when the project registers them:
//
//     struct MyDriverBackend : IBackend { ... };
//     Memory::RegisterBackend("mydriver", [] { return new MyDriverBackend(); });
//     mem.Init("Game-Win64-Shipping.exe", "mydriver");
//
// ReadUseCache adds a 4 KB page cache on top of the backend - important for
// slow backends (page-table walks per read) because the GObjects/FName walk
// re-reads fields on the same pages over and over.
// =============================================================================

#include <windows.h>

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <Memory/IBackend.h>

class Memory
{
public:
    using BackendFactory = std::function<IBackend* ()>;

    // Register a backend under a name. Call before Init().
    // The built-in "dma" and "rpm" backends are always registered.
    static void RegisterBackend(const std::string& name, BackendFactory factory);

    // Attach to a process. backendName selects a registered backend
    // ("dma" when empty).
    ~Memory() { Shutdown(); }
    bool Init(const std::string& processName, const std::string& backendName = "dma");
    void Shutdown();

    // ---- reads -------------------------------------------------------------
    // Small reads fail fast. Large reads (>16 MB) are chunked at 64 KB and
    // retry failed chunks by page and zero-fill unreadable pages (a copy of
    // a PE image legitimately contains unmapped/discarded pages).
    bool Read(uintptr_t address, void* buffer, size_t size);

    // Page-cached read (4 KB granularity), see class comment.
    bool ReadUseCache(uintptr_t address, void* buffer, size_t size);
    void InvalidateCache();
    bool RefreshTranslationCache();

    template <typename T>
    T Read(uint64_t address)
    {
        T buffer{};
        Read(address, &buffer, sizeof(T));
        return buffer;
    }

    template <typename T>
    T Read(void* address)
    {
        return Read<T>(reinterpret_cast<uint64_t>(address));
    }

    template <typename T>
    T ReadUseCache(uint64_t address)
    {
        T buffer{};
        ReadUseCache(address, &buffer, sizeof(T));
        return buffer;
    }

    template <typename T>
    T ReadUseCache(void* address)
    {
        return ReadUseCache<T>(reinterpret_cast<uint64_t>(address));
    }

    // ---- process info ------------------------------------------------------
    size_t GetBaseDaddy(const std::string& moduleName);
    size_t GetBaseSize(const std::string& moduleName);

    // Dump the whole module image to disk.
    bool DumpMemory(uintptr_t address, const std::string& path);

private:
    bool ReadBlockRaw(uintptr_t address, void* buffer, size_t size);

    static constexpr uint64_t kPageSize = 0x1000;
    static constexpr uint64_t kPageMask = ~(kPageSize - 1);

    IBackend* Backend = nullptr;
    bool Initialized = false;

    uintptr_t MainBase = 0;
    size_t MainSize = 0;
    std::string MainModule;

    std::unordered_map<uint64_t, std::vector<uint8_t>> PageCache;
    std::mutex CacheMutex;
};

extern Memory mem;
