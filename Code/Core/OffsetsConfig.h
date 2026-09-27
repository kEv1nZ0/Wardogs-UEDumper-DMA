#pragma once

// =============================================================================
// OffsetsConfig - loads config.ini next to the exe (manual offset overrides
// + AOB signature strings).
//
// Use case: after a game update, update the [AOB] signatures or paste new
// RVAs into [Offsets] without recompiling.
//
// File format (key = value per line, # or ; start comments, [Section] lines
// are ignored):
//   [Offsets]
//   GNames = 0xD1A9B68
//   UObject.Name = 0x18
//   [AOB]
//   AOB.GNames.Sig = 48 8D 0D ? ? ? ? 0F 10 00 0F 11 44 24 40
//   AOB.GNames.DispOffset = 0x3
//
// Values that fail numeric parsing (e.g. signature strings) are kept in a
// string map accessible via GetString.
// =============================================================================
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <windows.h>
#include "Offset.h"

namespace OffsetsConfig
{
    inline std::unordered_map<std::string, uint64_t> g_overrides;
    inline std::unordered_map<std::string, std::string> g_strings;

    inline std::string Trim(std::string s)
    {
        while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
        size_t i = 0;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        if (i) s.erase(0, i);
        return s;
    }

    inline bool ParseUInt64(const std::string& v, uint64_t& out)
    {
        try {
            size_t end = 0;
            // auto-detect 0x / decimal
            if (v.size() > 2 && (v[0] == '0') && (v[1] == 'x' || v[1] == 'X')) {
                out = std::stoull(v.substr(2), &end, 16);
            } else {
                out = std::stoull(v, &end, 0);
            }
            return true;
        } catch (...) {
            return false;
        }
    }

    inline bool GetBool(const std::string& key, bool defaultValue)
    {
        auto it = g_overrides.find(key);
        if (it == g_overrides.end()) return defaultValue;
        return it->second != 0;
    }

    inline uint64_t GetUInt(const std::string& key, uint64_t defaultValue)
    {
        auto it = g_overrides.find(key);
        if (it == g_overrides.end()) return defaultValue;
        return it->second;
    }

    inline std::string GetString(const std::string& key, const std::string& defaultValue)
    {
        auto it = g_strings.find(key);
        if (it == g_strings.end()) return defaultValue;
        return it->second;
    }

    inline void Load()
    {
        g_overrides.clear();
        g_strings.clear();

        char exePath[MAX_PATH]{};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        auto iniPath = std::filesystem::path(exePath).parent_path() / "config.ini";

        std::ifstream f(iniPath);
        if (!f.is_open()) {
            std::printf("[OffsetsConfig] config.ini not found, using built-in defaults only\n");
            return;
        }

        std::string line;
        int lineNo = 0, loaded = 0;
        while (std::getline(f, line)) {
            ++lineNo;
            line = Trim(line);
            if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            auto key = Trim(line.substr(0, eq));
            auto val = Trim(line.substr(eq + 1));
            // strip trailing inline comments
            auto hash = val.find_first_of("#;");
            if (hash != std::string::npos) val = Trim(val.substr(0, hash));
            if (val.empty()) continue;

            g_strings[key] = val;

            uint64_t v;
            if (ParseUInt64(val, v)) {
                g_overrides[key] = v;
                ++loaded;
            }
        }
        std::printf("[OffsetsConfig] loaded %d numeric values (%zu string values) from config.ini\n",
            loaded, g_strings.size());
    }

    // Apply the loaded overrides to the global Offset struct. Called at the
    // end of DumpOffset::Init (numeric keys win over AOB results).
    inline void Apply()
    {
        if (g_overrides.empty() && g_strings.empty()) return;

        auto pick = [&](const char* k, auto& dst) {
            auto it = g_overrides.find(k);
            if (it != g_overrides.end()) {
                using T = std::decay_t<decltype(dst)>;
                dst = static_cast<T>(it->second);
                std::printf("[OffsetsConfig] override %s = 0x%llX\n", k, (unsigned long long)it->second);
            }
        };

        // top-level RVAs
        pick("GNames", Offset.GNames);
        pick("GObjects", Offset.GObjects);
        pick("GWorld", Offset.GWorld);
        pick("GEngine", Offset.GEngine);
        pick("ChunkMask", Offset.ChunkMask);
        pick("NameMask", Offset.NameMask);
        pick("NumElementsPerChunk", Offset.NumElementsPerChunk);

        // FNamePool / FNameEntry layout
        pick("NamePoolBlocksOffset", Offset.NamePoolBlocksOffset);
        pick("NameEntryStride", Offset.NameEntryStride);
        pick("NameHeaderOffset", Offset.NameHeaderOffset);
        pick("NameDataOffset", Offset.NameDataOffset);
        pick("NameFixedKind", Offset.NameFixedKind);
        pick("NameFixedKindLen", Offset.NameFixedKindLen);

        // FUObjectArray header fields (relative to GObjects)
        pick("GUObjectArray.MaxElements", Offset.GUObjectArray.MaxElements);
        pick("GUObjectArray.NumElements", Offset.GUObjectArray.NumElements);
        pick("GUObjectArray.MaxChunks", Offset.GUObjectArray.MaxChunks);
        pick("GUObjectArray.NumChunks", Offset.GUObjectArray.NumChunks);

        // UObject
        pick("UObject.Flags", Offset.UObject.Flags);
        pick("UObject.Name", Offset.UObject.Name);
        pick("UObject.Index", Offset.UObject.Index);
        pick("UObject.Class", Offset.UObject.Class);
        pick("UObject.Outer", Offset.UObject.Outer);
        pick("UObject.Size", Offset.UObject.Size);
        pick("UObject.ItemObject", Offset.UObject.ItemObject);

        // UField
        pick("UField.Next", Offset.UField.Next);

        // FField
        pick("FField.Vft", Offset.FField.Vft);
        pick("FField.Class", Offset.FField.Class);
        pick("FField.Owner", Offset.FField.Owner);
        pick("FField.Next", Offset.FField.Next);
        pick("FField.Name", Offset.FField.Name);
        pick("FField.Flags", Offset.FField.Flags);

        // FFieldClass
        pick("FFieldClass.CastFlags", Offset.FFieldClass.CastFlags);

        // UStruct
        pick("UStruct.SuperStruct", Offset.UStruct.SuperStruct);
        pick("UStruct.Children", Offset.UStruct.Children);
        pick("UStruct.ChildProperties", Offset.UStruct.ChildProperties);
        pick("UStruct.PropertiesSize", Offset.UStruct.PropertiesSize);
        pick("UStruct.MinAlignemnt", Offset.UStruct.MinAlignemnt);

        // UEnum
        pick("UEnum.Names", Offset.UEnum.Names);

        // FName
        pick("FName.Size", Offset.FName.Size);

        // UFunction
        pick("UFunction.FunctionFlags", Offset.UFunction.FunctionFlags);
        pick("UFunction.Func", Offset.UFunction.Func);

        // UProperty
        pick("UProperty.ElementSize", Offset.UProperty.ElementSize);
        pick("UProperty.ArrayDim", Offset.UProperty.ArrayDim);
        pick("UProperty.PropertyFlags", Offset.UProperty.PropertyFlags);
        pick("UProperty.Offset", Offset.UProperty.Offset);
        pick("UProperty.Size", Offset.UProperty.Size);
        pick("UProperty.PropertyClass", Offset.UProperty.PropertyClass);
    }
}
