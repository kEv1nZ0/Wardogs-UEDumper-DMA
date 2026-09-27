#pragma once

#include <windows.h>
#include <string>
#include <Memory/Memory.h>
#include "Data.h"

namespace Process
{
	bool Init();

	std::vector<uint64_t> FindSignatureAll(const char* signature);

	template<typename T>
	T SwapEndian(T value)
	{
		static_assert(std::is_integral_v<T>, "Type must be integral");

		if constexpr (sizeof(T) == 1) {
			return value;
		}
		else if constexpr (sizeof(T) == 2) {
			return ((value & 0xFF00) >> 8) | ((value & 0x00FF) << 8);
		}
		else if constexpr (sizeof(T) == 4) {
			return ((value & 0xFF000000) >> 24) |
				((value & 0x00FF0000) >> 8) |
				((value & 0x0000FF00) << 8) |
				((value & 0x000000FF) << 24);
		}
		else if constexpr (sizeof(T) == 8) {
			return ((value & 0xFF00000000000000ULL) >> 56) |
				((value & 0x00FF000000000000ULL) >> 40) |
				((value & 0x0000FF0000000000ULL) >> 24) |
				((value & 0x000000FF00000000ULL) >> 8) |
				((value & 0x00000000FF000000ULL) << 8) |
				((value & 0x0000000000FF0000ULL) << 24) |
				((value & 0x000000000000FF00ULL) << 40) |
				((value & 0x00000000000000FFULL) << 56);
		}
	}

	// Buffer range check: guards against out-of-bounds access from stale AOB
	// signature hits.
	inline bool InBuffer(uint64_t addr, size_t bytes)
	{
		if (!GameData.Memory || !GameData.MemorySize) return false;
		uint64_t base = (uint64_t)GameData.Memory;
		return addr >= base && (addr + bytes) <= (base + GameData.MemorySize);
	}

	// Typed read (bounds-checked, returns T{} on failure).
	template<typename T>
	T Read(uint64_t address)
	{
		if (!InBuffer(address, sizeof(T))) return T{};
		return *(T*)(address);
	}

	template<typename T>
	T ReadBE(uint64_t address)
	{
		if (!InBuffer(address, sizeof(T))) return T{};
		return SwapEndian(*(T*)(address));
	}

	// RIP-relative resolve: reads the disp32 at `current` and returns the
	// target as a game-module RVA.
	inline uint64_t CalcRelative(uint64_t current, int32_t relative = 4)
	{
		if (!InBuffer(current, sizeof(int32_t))) return 0;
		return (current + *(int32_t*)current + relative) - (uint64_t)GameData.Memory;
	}
}
