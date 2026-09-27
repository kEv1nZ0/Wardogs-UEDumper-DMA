#include <windows.h>
#include <iostream>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cstdlib>

#include <Core/Data.h>

#include <Memory/Memory.h>
#include <Memory/DmaBackend.h>

#include <Core/Offset.h>
#include <Core/OffsetsConfig.h>
#include <Core/Process.h>
#include <Core/GName.h>
#include <Core/GObject.h>
#include <Core/DumpOffset.h>

FGameData GameData;
FOffset Offset;

static bool PauseAtExit = true;

static void Pause()
{
    if (PauseAtExit) std::system("pause");
}

int main(int argc, char* argv[])
{
    // Sources are compiled with /utf-8; switch the console too.
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--no-pause") {
            PauseAtExit = false;
        } else {
            std::printf("Usage: UEDumper.exe [--no-pause]\n");
            return 1;
        }
    }

    auto start = std::chrono::high_resolution_clock::now();

    std::cout << "[" << GameData.Name << "]" << " SDK + Offset Dump\n";

    // 1) load config.ini early (SafeMode / SkipAOB / Backend / offsets)
    OffsetsConfig::Load();

    // 2) configure DMA, then attach to the process on the target machine.
    DmaOptions dma;
    dma.Device = OffsetsConfig::GetString("DMA.Device", "fpga://algo=0");
    dma.Remote = OffsetsConfig::GetString("DMA.Remote", "");
    dma.MemMap = OffsetsConfig::GetString("DMA.MemMap", "none");
    dma.Debug = OffsetsConfig::GetBool("DMA.Debug", false);
    const uint64_t pid = OffsetsConfig::GetUInt("DMA.Pid", 0);
    if (pid > MAXDWORD) {
        std::printf("[ERR] DMA.Pid is outside the DWORD range\n");
        Pause();
        return 1;
    }
    dma.ProcessId = static_cast<uint32_t>(pid);
    Memory::RegisterBackend("dma", [dma] { return new DmaBackend(dma); });
    std::string backend = OffsetsConfig::GetString("Backend", "dma");
    if (!mem.Init(GameData.Name + ".exe", backend))
    {
        std::cout << "Memory backend init failed\n";
        Pause();
        return 1;
    }

    // 3) output directory
    {
        auto Root = std::filesystem::path(argv[0]);
        Root.remove_filename();
        GameData.Directory = Root / "Games" / GameData.Name;
        std::filesystem::create_directories(GameData.Directory);
    }

    // 4) record base + size
    GameData.Global.Base = mem.GetBaseDaddy(GameData.Name + ".exe");
    GameData.Global.Size = mem.GetBaseSize(GameData.Name + ".exe");

    std::printf("GameBase: 0x%llX - 0x%llX\n",
                GameData.Global.Base,
                GameData.Global.Base + GameData.Global.Size);

    if (!GameData.Global.Base || !GameData.Global.Size) {
        std::cout << "GetBase/Size failed\n";
        Pause();
        return 1;
    }

    OffsetsConfig::Apply();
    bool safeMode = OffsetsConfig::GetBool("SafeMode", true); // safe by default

    if (safeMode)
    {
        printf("\n========================================================\n");
        printf("  SAFE MODE enabled (SafeMode=1 in config.ini)\n");
        printf("  Skips the full module dump and the AOB scan.\n");
        printf("  Uses only the offsets from config.ini and validates the\n");
        printf("  first 10 UObject names.\n");
        printf("========================================================\n\n");

        if (!Offset.GNames || !Offset.GObjects) {
            printf("[ERR] config.ini must provide both GNames and GObjects\n");
            printf("      example (Wardogs UE 5.7):\n");
            printf("        GNames = 0xCE6CA30\n");
            printf("        GObjects = 0xCF3F260\n");
            Pause();
            return 1;
        }

        printf("Current offsets (from config.ini):\n");
        printf("  GNames              = 0x%llX\n", (unsigned long long)Offset.GNames);
        printf("  GObjects            = 0x%llX\n", (unsigned long long)Offset.GObjects);
        printf("  NumElementsPerChunk = 0x%llX\n", (unsigned long long)Offset.NumElementsPerChunk);
        printf("  ChunkMask           = %llu\n", (unsigned long long)Offset.ChunkMask);
        printf("  NameMask            = 0x%llX\n", (unsigned long long)Offset.NameMask);
        printf("  NamePoolBlocksOffset= 0x%llX\n", (unsigned long long)Offset.NamePoolBlocksOffset);
        printf("  NameEntryStride     = 0x%llX\n", (unsigned long long)Offset.NameEntryStride);
        printf("  NameHeaderOffset    = 0x%X\n", (unsigned)Offset.NameHeaderOffset);
        printf("  NameDataOffset      = 0x%X\n", (unsigned)Offset.NameDataOffset);
        printf("  UObject.Name        = 0x%X\n", (unsigned)Offset.UObject.Name);
        printf("  UObject.Class       = 0x%X\n", (unsigned)Offset.UObject.Class);
        printf("  UObject.Size (item) = 0x%X\n", (unsigned)Offset.UObject.Size);
        printf("  UObject.ItemObject  = 0x%X  (UObject* offset inside FUObjectItem)\n\n", (unsigned)Offset.UObject.ItemObject);

        // GObjects array header (Objects pointer)
        // Stock UE5 FChunkedFixedUObjectArray layout:
        //   0x00: Objects (chunk table pointer)
        //   0x10 MaxElements / 0x14 NumElements / 0x18 MaxChunks / 0x1C NumChunks
        UINT64 gobjBase = GameData.Global.Base + Offset.GObjects;
        UINT64 objArray = mem.Read<UINT64>(gobjBase);
        printf("GObjects array header: *(0x%llX) = 0x%llX\n", gobjBase, objArray);
        printf("NumElements(+0x14) = %u, NumChunks(+0x1C) = %u\n",
            mem.Read<UINT32>(gobjBase + Offset.GUObjectArray.NumElements),
            mem.Read<UINT32>(gobjBase + Offset.GUObjectArray.NumChunks));

        if (!objArray || objArray < 0x10000) {
            printf("[ERR] GObjects resolved to an invalid pointer - the RVAs in config.ini are probably wrong\n");
            Pause();
            return 1;
        }


        // Read the first 10 UObjects (chunked array: chunks[i] -> obj_item[j])
        printf("\nFirst 10 UObjects:\n");
        printf("  #   |  UObject address  | NameID    | Name\n");
        printf("  ----+-------------------+-----------+---------------------------\n");
        UINT64 chunk0 = mem.Read<UINT64>(objArray) & ~0xFULL;
        if (!chunk0) {
            printf("[ERR] chunk0 pointer is 0 - the FUObjectArray layout differs, the read path needs adjusting\n");
            Pause();
            return 1;
        }
        // Wardogs encoding (current DMA reader build):
        //   obj      = chunks[ID>>16] + (ID&0xFFFF) * 0x18   (FUObjectItem stride)
        //   UObject* = *(obj + 0x10)                         (low chunk tag bits are cleared)
        //   NameID   = *(u32*)(UObject + 0x18)
        for (int i = 0; i < 10; ++i) {
            UINT64 item = chunk0 + (UINT64)i * Offset.UObject.Size + Offset.UObject.ItemObject;
            UINT64 objAddr = mem.Read<UINT64>(item);
            if (!objAddr) { printf("  %-3d | <null>\n", i); continue; }
            UINT32 nameID = mem.Read<UINT32>(objAddr + Offset.UObject.Name);
            std::string name = GName::ResolveName(nameID);
            printf("  %-3d | 0x%016llX | 0x%08X | %s\n", i, objAddr, nameID, name.c_str());
        }

        printf("\n========================================================\n");
        printf("  Check the 10 names above: if they are plausible UE object\n");
        printf("  names (e.g. \"/Script/CoreUObject\", \"Object\"), then\n");
        printf("  GNames/GObjects/ChunkMask/NameMask are all correct.\n");
        printf("========================================================\n");

        Pause();
        return 0;
    }

    // ============================================================
    // Full mode
    // ============================================================
    printf("\n[WARN] SafeMode=0, running the full dump flow\n");

    // optional: dump the module to disk first
    // mem.DumpMemory(GameData.Global.Base, GameData.Name + "-Dump.exe");

    // copy the whole game module into a local buffer for AOB scanning
    if (!Process::Init()) {
        std::cout << "Process::Init failed\n";
        Pause();
        return 1;
    }

    // resolve key offsets
    DumpOffset::Init();

    // parse the FName pool
    if (!GName::Init()) {
        delete[] GameData.Memory;
        GameData.Memory = nullptr;
        GameData.MemorySize = 0;
        Pause();
        return 1;
    }

    // parse the GObject table
    GObject::Init();

    // generate the SDK
    DumpOffset::SDK();

    auto end = std::chrono::high_resolution_clock::now();
    double seconds = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() / 1000.0;
    std::printf("\nDone. %fs\n", seconds);

    delete[] GameData.Memory;
    GameData.Memory = nullptr;
    GameData.MemorySize = 0;
    Pause();
    return 0;
}
