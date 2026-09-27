#pragma once

#include <windows.h>
#include <filesystem>
#include <iostream>
#include <unordered_map>
#include <Memory/Memory.h>

struct FGameData
{
	std::string Name = "WardogsClient-Win64-Shipping";
	uint8_t* Memory = nullptr;
	size_t MemorySize = 0;
	std::filesystem::path Directory;

	struct
	{
		UINT64 Base = 0;
		UINT64 Size = 0;
	} Global;

	struct
	{
		std::unordered_map<int, std::string> FNameTables;
	} GName;

	struct
	{
		std::unordered_map<std::string, UINT64> NameToAddress;
		std::vector<UINT64> Array;
	} GObject;

	struct
	{
		std::unordered_map<std::string, UINT32> NameToOffset;
	} Offset;
};


extern FGameData GameData;
