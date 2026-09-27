#pragma once

#include <windows.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <format>
#include <string>
#include <vector>
#include <Memory/Memory.h>
#include <Core/Data.h>
#include <Core/Offset.h>
#include <Core/Wrappers.h>

namespace GName
{
	constexpr size_t MaxNameLength = 1023; // Ten length bits in the FName header.
	constexpr uint32_t MaxBlocks = 0x1000;

	struct Layout
	{
		size_t BlockBytes = 0;
		size_t PrefixBytes = 0;
	};

	inline bool GetLayout(Layout& layout)
	{
		// Bound shifts, ID arithmetic, allocations and every configurable field.
		if (Offset.ChunkMask == 0 || Offset.ChunkMask > 19 ||
			Offset.NameMask != (1ULL << Offset.ChunkMask) - 1 ||
			Offset.NameEntryStride == 0 || Offset.NameEntryStride > 128 ||
			(Offset.NameEntryStride & (Offset.NameEntryStride - 1)) != 0 ||
			Offset.NameDataOffset > 4096 ||
			(Offset.NameFixedKind && (!Offset.NameFixedKindLen || Offset.NameFixedKindLen > MaxNameLength)))
			return false;
		layout.PrefixBytes = static_cast<size_t>(Offset.NameHeaderOffset) + (Offset.NameFixedKind ? 3 : 2);
		layout.BlockBytes = static_cast<size_t>((Offset.NameMask + 1) * Offset.NameEntryStride);
		return Offset.NameDataOffset >= layout.PrefixBytes &&
			layout.BlockBytes >= Offset.NameDataOffset && layout.BlockBytes <= 0x800000;
	}

	inline bool ValidRange(uint64_t address, size_t size)
	{
		return address >= 0x10000 && address < 0x800000000000ULL &&
			size <= 0x800000000000ULL - address;
	}

	inline size_t AlignEntry(size_t bytes)
	{
		return (bytes + static_cast<size_t>(Offset.NameEntryStride) - 1) &
			~(static_cast<size_t>(Offset.NameEntryStride) - 1);
	}

	struct Entry
	{
		size_t Length = 0;
		size_t Bytes = 0;
		bool Wide = false;
	};

	enum class EntryState { Name, End, Invalid };

	inline EntryState ReadEntry(const uint8_t* bytes, Entry& entry)
	{
		uint16_t header = 0;
		std::memcpy(&header, bytes + Offset.NameHeaderOffset, sizeof(header));
		const size_t originalLength = header >> 6;
		const uint8_t kind = Offset.NameFixedKind ? bytes[Offset.NameHeaderOffset + 2] : 0;
		const bool fixed = Offset.NameFixedKind && kind == Offset.NameFixedKind;
		// An allocator terminator only clears length bits. Other bits and the
		// unused kind byte can contain stale data in the remainder of a block.
		if (!originalLength && !fixed) return EntryState::End;
		if (kind && kind != 1 && !fixed) return EntryState::Invalid;
		entry.Length = fixed ? Offset.NameFixedKindLen : originalLength;
		entry.Wide = (header & 1) != 0;
		if (!entry.Length || entry.Length > MaxNameLength) return EntryState::Invalid;

		// Wardogs kind 1 restores the original text and kind 2 displays a
		// fixed-length replacement. Both retain storage for the larger string.
		// Allocation length MUST NOT be replaced with the displayed length.
		const size_t storedLength = kind ? (std::max)(originalLength, size_t(Offset.NameFixedKindLen)) : originalLength;
		entry.Bytes = AlignEntry(Offset.NameDataOffset + storedLength * (entry.Wide ? 2 : 1));
		return EntryState::Name;
	}

	inline bool DecodeText(const uint8_t* bytes, const Entry& entry, std::string& name)
	{
		if (entry.Wide) {
			std::wstring wide(entry.Length, L'\0');
			std::memcpy(wide.data(), bytes, entry.Length * sizeof(wchar_t));
			if (wide.find(L'\0') != std::wstring::npos) return false;
			const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
				static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
			if (length <= 0) return false;
			name.resize(length);
			return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
				static_cast<int>(wide.size()), name.data(), length, nullptr, nullptr) == length;
		}
		name.assign(reinterpret_cast<const char*>(bytes), entry.Length);
		return name.find('\0') == std::string::npos &&
			MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(),
				static_cast<int>(name.size()), nullptr, 0) > 0;
	}

	inline bool BlockTable(uint64_t& address)
	{
		if (!ValidRange(GameData.Global.Base, 1) ||
			Offset.GNames >= 0x800000000000ULL - GameData.Global.Base) return false;
		const uint64_t pool = GameData.Global.Base + Offset.GNames;
		if (Offset.NamePoolBlocksOffset >= 0x800000000000ULL - pool) return false;
		address = pool + Offset.NamePoolBlocksOffset;
		return ValidRange(address, MaxBlocks * sizeof(uint64_t));
	}

	inline std::string ResolveName(UINT64 id)
	{
		Layout layout;
		uint64_t table = 0, block = 0;
		if (!GetLayout(layout) || !BlockTable(table) || (id >> Offset.ChunkMask) >= MaxBlocks)
			return "FAIL";
		if (!mem.ReadUseCache(table + (id >> Offset.ChunkMask) * 8, &block, sizeof(block)) ||
			!ValidRange(block, layout.BlockBytes)) return "FAIL";
		const size_t offset = static_cast<size_t>((id & Offset.NameMask) * Offset.NameEntryStride);
		if (layout.PrefixBytes > layout.BlockBytes - offset) return "FAIL";
		std::vector<uint8_t> prefix(layout.PrefixBytes);
		if (!mem.ReadUseCache(block + offset, prefix.data(), prefix.size())) return "FAIL";
		Entry entry;
		if (ReadEntry(prefix.data(), entry) != EntryState::Name || entry.Bytes > layout.BlockBytes - offset)
			return "FAIL";
		std::vector<uint8_t> text(entry.Length * (entry.Wide ? 2 : 1));
		std::string name;
		if (!mem.Read(block + offset + Offset.NameDataOffset, text.data(), text.size()) ||
			!DecodeText(text.data(), entry, name)) return "FAIL";
		return name;
	}

	struct PoolSnapshot
	{
		uint32_t CurrentBlock = 0;
		uint32_t Cursor = 0;
		std::vector<uint64_t> Blocks;
	};

	inline bool Snapshot(const Layout& layout, PoolSnapshot& snapshot, std::string& error)
	{
		uint64_t table = 0;
		if (Offset.NamePoolBlocksOffset < 8 || !BlockTable(table)) {
			error = "Invalid name-pool anchor or block table offset (allocator counters must precede the table).";
			return false;
		}
		for (int attempt = 0; attempt < 3; ++attempt) {
			uint32_t before[2]{}, after[2]{};
			// Relative to the table, so manual 0xCE6CA30/+0x20 and AOB
			// 0xCE6CA40/+0x10 read the same allocator and block pointers.
			if (!mem.Read(table - 8, before, sizeof(before))) {
				error = "Cannot read name-pool allocator counters.";
				return false;
			}
			if (before[0] >= MaxBlocks || before[1] > layout.BlockBytes || before[1] % Offset.NameEntryStride) {
				error = "Invalid name-pool block/cursor; check GNames and NamePoolBlocksOffset.";
				return false;
			}
			snapshot.Blocks.resize(static_cast<size_t>(before[0]) + 1);
			if (!mem.Read(table, snapshot.Blocks.data(), snapshot.Blocks.size() * sizeof(uint64_t)) ||
				!mem.Read(table - 8, after, sizeof(after))) {
				error = "Cannot snapshot the name-pool block table.";
				return false;
			}
			if (before[0] != after[0] || after[1] < before[1] || after[1] > layout.BlockBytes ||
				after[1] % Offset.NameEntryStride) continue;
			for (const auto block : snapshot.Blocks) {
				if (!ValidRange(block, layout.BlockBytes) || block % Offset.NameEntryStride) {
					error = "Invalid pointer in the active name-pool block table.";
					return false;
				}
			}
			snapshot.CurrentBlock = before[0];
			snapshot.Cursor = before[1]; // A fixed prefix: names appended later belong to the next dump.
			return true;
		}
		error = "Name-pool allocator changed during three snapshot attempts; retry the dump.";
		return false;
	}

	struct DumpStats
	{
		size_t Names = 0;
		uint32_t HighestID = 0;
		uint32_t Blocks = 0;
		uint32_t Cursor = 0;
	};

	inline bool ReadPoolBlock(uint32_t block, uint64_t address, uint8_t* bytes, size_t size,
		std::string& error, bool progress)
	{
		if (!size || mem.Read(address, bytes, size)) return true;
		const bool refreshed = mem.RefreshTranslationCache();
		if (progress) {
			std::printf("\n[GName] Block %u bulk read failed; retrying by 4 KB page (translation refresh: %s).\n",
				block, refreshed ? "ok" : "unsupported/failed");
		}
		// Keep the fast bulk path, then recover failed DMA reads at page
		// boundaries. Never zero-fill a missing page into a name-pool snapshot.
		constexpr size_t PageSize = 0x1000;
		constexpr unsigned Attempts = 3;
		for (size_t offset = 0; offset < size;) {
			const uint64_t current = address + offset;
			const size_t take = (std::min)(size - offset, PageSize - size_t(current & (PageSize - 1)));
			bool read = false;
			for (unsigned attempt = 0; attempt < Attempts; ++attempt) {
				if (mem.Read(current, bytes + offset, take)) {
					read = true;
					break;
				}
				if (attempt + 1 < Attempts) Sleep(10);
			}
			if (!read) {
				std::memset(bytes, 0, size);
				error = std::format("Cannot read name-pool block {}: page 0x{:X}, block +0x{:X}, {} bytes, after {} attempts (translation refresh: {}).",
					block, current, offset, take, Attempts, refreshed ? "ok" : "unsupported/failed");
				return false;
			}
			offset += take;
		}
		if (progress) std::printf("[GName] Block %u recovered by page reads.\n", block);
		return true;
	}

	template<typename Visitor>
	inline bool Enumerate(Visitor&& visit, DumpStats& stats, std::string& error, bool progress = false)
	{
		stats = {};
		error.clear();
		Layout layout;
		PoolSnapshot snapshot;
		if (!GetLayout(layout)) {
			error = "Invalid FName entry layout.";
			return false;
		}
		if (!Snapshot(layout, snapshot, error)) return false;
		stats.Blocks = snapshot.CurrentBlock + 1;
		stats.Cursor = snapshot.Cursor;
		std::vector<uint8_t> bytes(layout.BlockBytes);
		for (uint32_t block = 0; block < stats.Blocks; ++block) {
			const bool last = block == snapshot.CurrentBlock;
			const size_t limit = last ? snapshot.Cursor : layout.BlockBytes;
			if (!ReadPoolBlock(block, snapshot.Blocks[block], bytes.data(), limit, error, progress)) return false;
			for (size_t offset = 0; offset < limit;) {
				const size_t remaining = limit - offset;
				if (!last && remaining < layout.PrefixBytes) break;
				Entry entry;
				const auto state = remaining >= layout.PrefixBytes ? ReadEntry(bytes.data() + offset, entry) : EntryState::Invalid;
				// Completed blocks have an unused tail after a zero-length marker.
				// The live cursor never includes such a marker or a partial entry.
				if (!last && state == EntryState::End &&
					remaining <= AlignEntry(Offset.NameDataOffset + MaxNameLength * 2)) break;
				std::string name;
				if (state != EntryState::Name || entry.Bytes > remaining ||
					!DecodeText(bytes.data() + offset + Offset.NameDataOffset, entry, name)) {
					error = std::format("Invalid FName entry at block {} +0x{:X} (limit 0x{:X}).", block, offset, limit);
					return false;
				}
				const uint32_t id = static_cast<uint32_t>((uint64_t(block) << Offset.ChunkMask) | (offset / Offset.NameEntryStride));
				if (!stats.Names && (id != 0 || name != "None")) {
					error = "FName ID 0 is not None; check the name-pool layout.";
					return false;
				}
				if (!visit(id, name)) {
					error = std::format("Cannot write FName ID {}.", id);
					return false;
				}
				++stats.Names;
				stats.HighestID = id;
				offset += entry.Bytes;
			}
			if (progress) std::printf("\rNames: %zu  blocks: %u/%u", stats.Names, block + 1, stats.Blocks);
		}
		if (!stats.Names) {
			error = "Name pool is empty; no None anchor was found.";
			return false;
		}
		return true;
	}

	inline bool Init()
	{
		std::printf("\n[GName]\n");
		const auto destination = GameData.Directory / "NamesDump.txt";
		const auto partial = GameData.Directory / "NamesDump.txt.partial";
		std::ofstream output(partial, std::ios::binary | std::ios::trunc);
		GameData.GName.FNameTables.clear();
		if (!output) {
			std::printf("[ERR] Cannot create NamesDump.txt.partial\n");
			return false;
		}
		DumpStats stats;
		std::string error;
		bool ok = Enumerate([&](uint32_t id, const std::string& name) {
			output << std::format("[{:09d}] {}\n", id, name);
			if (!output) return false;
			GameData.GName.FNameTables.emplace(static_cast<int>(id), name);
			return true;
		}, stats, error, true);
		output.close();
		if (ok && !output) {
			ok = false;
			error = "Cannot flush/close NamesDump.txt.partial.";
		}
		if (ok && !MoveFileExW(partial.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			ok = false;
			error = std::format("Cannot publish NamesDump.txt (Windows error {}).", GetLastError());
		}
		if (!ok) {
			GameData.GName.FNameTables.clear();
			std::printf("\n[ERR] NamesDump failed: %s\n[ERR] NamesDump.txt was not replaced.\n", error.c_str());
			return false;
		}
		std::printf("\nNamesDump complete: %zu names, %u blocks, cursor=0x%X, highest ID=%u\n",
			stats.Names, stats.Blocks, stats.Cursor, stats.HighestID);
		return true;
	}
}
