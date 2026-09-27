#include "Process.h"
#include <Memory/Memory.h>
#include <Core/Data.h>
#include <new>

bool Process::Init()
{
	auto startTime = std::chrono::high_resolution_clock::now();

	auto Base = GameData.Global.Base;

	size_t ImageSize = GameData.Global.Size;
	if (!Base || !ImageSize) return false;
	delete[] GameData.Memory;
	GameData.Memory = new (std::nothrow) uint8_t[ImageSize]{};
	GameData.MemorySize = 0;
	if (!GameData.Memory) return false;
	GameData.MemorySize = ImageSize;

	printf("GameData.Memory: 0x%llX - 0x%llX\n", (uint64_t)GameData.Memory, (uint64_t)GameData.Memory + ImageSize);

	if (!mem.Read(Base, GameData.Memory, ImageSize)) {
		printf("[ERR] Module copy failed: too little readable memory\n");
		delete[] GameData.Memory;
		GameData.Memory = nullptr;
		GameData.MemorySize = 0;
		return false;
	}

	auto endTime = std::chrono::high_resolution_clock::now();

	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

	double sizeInMB = ImageSize / (1024.0 * 1024.0);
	double speedMBps = (duration > 0) ? (sizeInMB * 1000.0 / duration) : 0;

	printf("Dump Memory Succeed: %f MB %f MB/s\n", sizeInMB, speedMBps);

	return true;
}

static const char* hexdigits =
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\001\002\003\004\005\006\007\010\011\000\000\000\000\000\000"
"\000\012\013\014\015\016\017\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\012\013\014\015\016\017\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000";

static uint8_t GetByte(const char* hex)
{
	return (uint8_t)((hexdigits[hex[0]] << 4) | (hexdigits[hex[1]]));
}

std::vector<uint64_t> Process::FindSignatureAll(const char* signature)
{
	const uint8_t* memory_start = GameData.Memory;
	const uint8_t* memory_end = GameData.Memory + GameData.MemorySize;
	std::vector<uint64_t> results;

	// pattern length
	size_t pattern_length = 0;
	const char* p = signature;
	while (*p)
	{
		if (*p != ' ')
		{
			pattern_length++;
			if (*p == '?')
			{
				p++;
				if (*p) p++;
			}
			else
			{
				p++;
				if (*p) p++;
			}
		}
		else
		{
			p++;
		}
	}

	// brute-force scan over the whole local module copy
	for (const uint8_t* current_ptr = memory_start;
		current_ptr < memory_end - pattern_length + 1;
		current_ptr++)
	{
		bool match = true;
		const char* pat = signature;

		for (size_t j = 0; j < pattern_length; j++)
		{
			while (*pat == ' ') pat++;

			if (*pat == '?')
			{
				pat += 2; // skip '?'
				continue;
			}

			uint8_t expected = GetByte(pat);
			if (current_ptr[j] != expected)
			{
				match = false;
				break;
			}

			pat += 2;
		}

		if (match)
		{
			results.push_back((uint64_t)(current_ptr - memory_start) + (uint64_t)GameData.Memory);
		}
	}

	// empty result: print a warning and return a placeholder (buffer start) so
	// downstream Results[0] access never goes out of range.
	if (results.empty())
	{
		std::printf("[WARN] AOB not found: %s\n", signature);
		results.push_back((uint64_t)GameData.Memory);
	}

	return results;
}
