#include "Memory.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>

#include "RpmBackend.h"
#include "DmaBackend.h"

Memory mem;

// ---- backend registry ------------------------------------------------------

static std::unordered_map<std::string, Memory::BackendFactory>& Registry()
{
    static std::unordered_map<std::string, Memory::BackendFactory> registry = [] {
        std::unordered_map<std::string, Memory::BackendFactory> r;
        r["rpm"] = [] { return static_cast<IBackend*>(new RpmBackend()); };
        r["dma"] = [] { return static_cast<IBackend*>(new DmaBackend()); };
        return r;
    }();
    return registry;
}

void Memory::RegisterBackend(const std::string& name, BackendFactory factory)
{
    Registry()[name] = std::move(factory);
}

// ---- lifecycle -------------------------------------------------------------

bool Memory::Init(const std::string& processName, const std::string& backendName)
{
    Shutdown();
    std::string name = backendName.empty() ? "dma" : backendName;

    auto& registry = Registry();
    auto it = registry.find(name);
    if (it == registry.end()) {
        std::printf("[Memory] unknown backend '%s' (registered:", name.c_str());
        for (const auto& kv : registry) std::printf(" %s", kv.first.c_str());
        std::printf(")\n");
        return false;
    }

    Backend = it->second();
    if (!Backend || !Backend->Attach(processName)) {
        std::printf("[Memory] backend '%s' failed to attach to %s\n", name.c_str(), processName.c_str());
        Shutdown();
        return false;
    }

    MainModule = processName;
    MainBase = Backend->GetModuleBase(processName);
    if (!MainBase) {
        std::printf("[Memory] failed to resolve module base for %s\n", processName.c_str());
        Shutdown();
        return false;
    }

    // Use the backend's module map; DMA does not require a readable PE header.
    MainSize = Backend->GetModuleSize(processName);
    if (!MainSize) {
        std::printf("[Memory] failed to resolve module size for %s\n", processName.c_str());
        Shutdown();
        return false;
    }

    Initialized = true;

    std::printf("[Memory] attached via '%s': base=0x%llX size=0x%llX\n",
                Backend->GetName(),
                (unsigned long long)MainBase,
                (unsigned long long)MainSize);
    return true;
}

void Memory::Shutdown()
{
    if (Backend) {
        Backend->Shutdown();
        delete Backend;
        Backend = nullptr;
    }
    Initialized = false;
    MainBase = 0;
    MainSize = 0;
    MainModule.clear();
    InvalidateCache();
}

// ---- reads -----------------------------------------------------------------

bool Memory::ReadBlockRaw(uintptr_t address, void* buffer, size_t size)
{
    if (!Initialized || !buffer || !size ||
        address > (std::numeric_limits<uintptr_t>::max)() - (size - 1)) return false;

    // 64 KB chunks: a failed chunk only poisons itself, not the whole read.
    constexpr size_t kChunk = 0x10000;
    uint8_t* dst = reinterpret_cast<uint8_t*>(buffer);
    size_t remaining = size;
    uintptr_t cur = address;

    size_t failedBytes = 0, readableBytes = 0;
    size_t lastReportedMB = 0;
    const bool bigRead = size > (16 * 1024 * 1024);

    while (remaining) {
        size_t req = remaining > kChunk ? kChunk : remaining;
        if (Backend->ReadRaw(cur, dst, req)) {
            readableBytes += req;
        } else {
            if (!bigRead) {
                std::memset(buffer, 0, size);
                return false;
            }
            // Retry failed chunks at page boundaries. One discarded PE page
            // must not erase the other readable pages in a 64 KB DMA read.
            for (size_t offset = 0; offset < req;) {
                size_t take = kPageSize - ((cur + offset) & (kPageSize - 1));
                if (take > req - offset) take = req - offset;
                if (Backend->ReadRaw(cur + offset, dst + offset, take)) {
                    readableBytes += take;
                } else {
                    std::memset(dst + offset, 0, take);
                    failedBytes += take;
                }
                offset += take;
            }
        }
        dst += req;
        cur += req;
        remaining -= req;

        if (bigRead) {
            size_t doneMB = (size - remaining) / (1024 * 1024);
            if (doneMB >= lastReportedMB + 32) {
                std::printf("[Memory] dump progress %zu/%zu MB  unreadable=%zu KB\n",
                            doneMB, size / (1024 * 1024), failedBytes / 1024);
                lastReportedMB = doneMB;
            }
        }
    }

    if (bigRead) {
        std::printf("[Memory] dump finished readable=%zu unreadable=%zu (%.1f%% readable)\n",
                    readableBytes, failedBytes, readableBytes * 100.0 / size);
        return readableBytes > failedBytes;
    }
    return true;
}

bool Memory::Read(uintptr_t address, void* buffer, size_t size)
{
    return ReadBlockRaw(address, buffer, size);
}

bool Memory::ReadUseCache(uintptr_t address, void* buffer, size_t size)
{
    if (!Initialized || !buffer || !size ||
        address > (std::numeric_limits<uintptr_t>::max)() - (size - 1)) return false;

    std::lock_guard<std::mutex> lock(CacheMutex);
    uint8_t* dst = reinterpret_cast<uint8_t*>(buffer);
    uint64_t remaining = size;
    uint64_t cur = address;

    while (remaining) {
        uint64_t page = cur & kPageMask;
        uint64_t pageOff = cur - page;
        uint64_t take = kPageSize - pageOff;
        if (take > remaining) take = remaining;

        auto it = PageCache.find(page);
        if (it == PageCache.end()) {
            // Failed/partial pages are never cached, so a transient DMA
            // failure can recover on the next read.
            std::vector<uint8_t> pageData(kPageSize, 0);
            if (!Backend->ReadRaw(static_cast<uintptr_t>(page), pageData.data(), static_cast<size_t>(kPageSize))) {
                std::memset(buffer, 0, size);
                return false;
            }
            it = PageCache.emplace(page, std::move(pageData)).first;
        }
        std::memcpy(dst, it->second.data() + pageOff, static_cast<size_t>(take));

        dst += take;
        cur += take;
        remaining -= take;
    }
    return true;
}

void Memory::InvalidateCache()
{
    std::lock_guard<std::mutex> lock(CacheMutex);
    PageCache.clear();
}

bool Memory::RefreshTranslationCache()
{
    InvalidateCache();
    return Initialized && Backend && Backend->RefreshTranslationCache();
}

// ---- process info ----------------------------------------------------------

size_t Memory::GetBaseDaddy(const std::string& moduleName)
{
    if (Backend && moduleName == MainModule) return MainBase;
    return Backend ? Backend->GetModuleBase(moduleName) : 0;
}

size_t Memory::GetBaseSize(const std::string& moduleName)
{
    if (Backend && moduleName == MainModule) return MainSize;
    return Backend ? Backend->GetModuleSize(moduleName) : 0;
}

bool Memory::DumpMemory(uintptr_t address, const std::string& path)
{
    size_t size = MainSize;
    if (!size) return false;

    std::vector<uint8_t> buf(size);
    if (!Read(address, buf.data(), size)) return false;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::printf("[Memory] DumpMemory: cannot open %s\n", path.c_str());
        return false;
    }
    out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(size));
    out.close();
    if (!out) return false;
    std::printf("[Memory] DumpMemory done: %s (%.2f MB)\n",
                path.c_str(), size / (1024.0 * 1024.0));
    return true;
}
