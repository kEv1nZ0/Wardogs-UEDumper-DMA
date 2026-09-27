#pragma once

#include <iostream>
#include <Memory/Memory.h>
#include <Core/Data.h>

// Default offsets / structure layouts (Wardogs client baseline).
// Every field can be overridden from config.ini: numeric keys in [Offsets]
// take priority over [AOB] signature scan results.

struct FOffset
{
	// Manual GNames RVA anchors the pool at 0xCE6CA30 (no dereference).
	// Verified live: block table +0x20; the retained AOB resolves 0xCE6CA40,
	// which requires block table +0x10 instead.
	// Readers that support this padded layout probe compatible name-pool
	// layouts at runtime:
	//   entry    = blocks[BlockIdx] + NameEntryStride * (ID & NameMask)
	//   entry+0x00: u64 string hash (used for lookup, skipped when reading)
	//   entry+0x08: u16 header (bit0=wide, bit1..5=probe hash, bit6..15=len,
	//               i.e. len = hdr >> 6)
	//   entry+0x0A: u8  kind  (== NameFixedKind marks fixed-length names of
	//               NameFixedKindLen chars)
	//   entry+0x0C: string data (narrow) / UTF-16 data (wide)
	UINT64 GNames = 0x0CE6CA30;

	UINT64 ChunkMask = 16;             // BlockIdx = ID >> 16
	UINT64 NameMask = 0xFFFF;          // slot     = ID & 0xFFFF

	UINT64 NamePoolBlocksOffset = 0x20;  // live verified with GNames = 0xCE6CA30
	UINT64 NameEntryStride = 8;          // slot unit in bytes (stock UE5 = 2)
	UINT16 NameHeaderOffset = 0x8;       // u16 header offset inside entry (stock UE5 = 0)
	UINT16 NameDataOffset = 0xC;         // string offset inside entry (stock UE5 = 2)
	UINT8  NameFixedKind = 2;            // kind byte value -> fixed length (0 = disabled)
	UINT16 NameFixedKindLen = 22;        // fixed-length char count
	// For block enumeration, kind 1 and kind 2 reserve storage for
	// max(header length, NameFixedKindLen), even when the displayed name is shorter.

	// FUObjectArray (FChunkedFixedUObjectArray) - stock UE5 layout, header
	// fields relative to GObjects.
	// Reference layout: +0x00 = Objects (chunk table pointer), +0x14 = NumElements
	UINT64 GObjects = 0x0CF3F260;

	// Reference RVAs (not used by the core dump flow; resolved by AOB for
	// external tooling).
	UINT64 GWorld = 0x0D0CB998;
	UINT64 GEngine = 0x0D0CEC50; // live GameEngine object; 0xD0C8AD0 reads null

	struct
	{
		UINT16 MaxElements = 0x10;
		UINT16 NumElements = 0x14;
		UINT16 MaxChunks = 0x18;
		UINT16 NumChunks = 0x1C;
	} GUObjectArray;

	// Entries per chunk: the Wardogs index encoding uses slot = ID & 0xFFFF,
	// so this must be 0x10000.
	UINT64 NumElementsPerChunk = 0x10000;

	struct
	{
		// FUObjectItem layout for this engine revision:
		//   +0x00 u64 flags/GC state (bit 57 is a validity check bit, never
		//         used as a pointer)
		//   +0x10 UObject*
		// UObject body members are stock UE5:
		//   +0x08 ObjectFlags(i32), +0x0C InternalIndex(i32), +0x10 Class,
		//   +0x18 Name(FName u32 id), +0x20 Outer
		UINT16 Flags = 0x8;
		UINT16 Name = 0x18;
		UINT16 Index = 0xC;
		UINT16 Class = 0x10;
		UINT16 Outer = 0x20;

		UINT16 Size = 0x18;            // FUObjectItem stride
		UINT16 ItemObject = 0x10;      // +0x08 is the flags field
	} UObject;

	struct
	{
		UINT16 Next{};
	} UField;

	// FField layout (engine modification, nailed down via runtime probing +
	// output cross-checking): the ONLY change vs stock UE5 is that the
	// FFieldVariant was shrunk from 16 to 8 bytes (pointer only, the type tag
	// was removed), shifting Next from 0x20 to 0x18 and Name from 0x28 to
	// 0x20. Evidence: the Next pointer-pair probe passes at 0x18 (with a
	// 16-byte variant +0x18 would hold the type tag and could never pass the
	// vft check); a Name scan over 0x28..0x7C found nothing (the real spot
	// 0x20 is left of the old scan start); and old dumps showing "correct
	// field counts/types + garbage names" are exactly consistent with
	// Next@0x18 plus a wrong Name. The UStruct side (SuperStruct@0x40 /
	// Children@0x48 / ChildProperties@0x50 / PropertiesSize@0x58) is stock.
	struct
	{
		UINT16 Vft = 0x00;
		UINT16 Class = 0x8;
		UINT16 Owner = 0x10;  // 8 bytes (stock FFieldVariant is 16)
		UINT16 Next = 0x18;   // Wardogs-modified (stock = 0x20)
		UINT16 Name = 0x20;   // Wardogs-modified (stock = 0x28)
		UINT16 Flags = 0x28;  // Name + 8
	} FField;

	struct
	{
		INT32 CastFlags = 0x10;
	} FFieldClass;

	struct
	{
		UINT16 SuperStruct{};
		UINT16 Children{};
		UINT16 ChildProperties{};
		UINT16 PropertiesSize{};
		UINT16 MinAlignemnt{};
	} UStruct;

	struct
	{
		UINT16 Names{};
	} UEnum;

	struct
	{
		UINT16 Size = 0x8; // stock UE5 FName = {u32 id, u32 number}
	} FName;

	struct
	{
		UINT16 FunctionFlags{};
		UINT16 Func{}; // ExecFunction
	} UFunction;

	struct
	{
		UINT16 ElementSize = 0x40;
		UINT16 ArrayDim = 0x44;
		UINT16 PropertyFlags = 0x38;
		UINT16 Offset = 0x60;
		UINT16 Size = 0x80;
		// Start of the FProperty subclass field area: the type pointers of
		// object/struct/enum properties, array Inner, map K/V, bool masks -
		// every subclass-specific read derives from this base. The old code
		// mistakenly used Size as this base, which resolved every type name
		// to garbage ("FNone").
		UINT16 PropertyClass = 0x80;
	} UProperty;
};

extern FOffset Offset;

namespace OffsetUtils
{
	inline bool IsInProcessRange(uintptr_t Address)
	{
		IMAGE_DOS_HEADER dosHeader = {};
		IMAGE_NT_HEADERS ntHeader = {};

		uintptr_t ImageBase = GameData.Global.Base;

		if (!mem.Read((uintptr_t)ImageBase, &dosHeader, sizeof(dosHeader)))
		{
			return false;
		}

		uintptr_t ntHeadersAddr = ImageBase + dosHeader.e_lfanew;

		if (!mem.Read((uintptr_t)ntHeadersAddr, &ntHeader, sizeof(ntHeader)))
		{
			return false;
		}

		return Address > ImageBase && Address < (ntHeader.OptionalHeader.SizeOfImage + ImageBase);
	}

	template<typename T>
	constexpr T Align(T Size, T Alignment)
	{
		static_assert(std::is_integral_v<T>, "Align can only handle integral types!");

		const T RequiredAlign = Alignment - (Size % Alignment);

		return Size + (RequiredAlign != Alignment ? RequiredAlign : 0x0);
	}

	template<bool bCheckForVft = true>
	inline int32_t GetValidPointerOffset(UINT64 ObjAAddress, UINT64 ObjBAddress, int32_t StartingOffset, int32_t MaxOffset)
	{
		for (int j = StartingOffset; j <= MaxOffset; j += sizeof(UINT64))
		{
			UINT64 pointerValueA = 0;
			UINT64 pointerValueB = 0;

			if (!mem.ReadUseCache(ObjAAddress + j, &pointerValueA, sizeof(UINT64)))
				continue;

			if (!mem.ReadUseCache(ObjBAddress + j, &pointerValueB, sizeof(UINT64)))
				continue;

			bool bIsAValid = (pointerValueA != 0);
			bool bIsBValid = (pointerValueB != 0);

			if (bCheckForVft && bIsAValid)
			{
				UINT64 vftPointerValueA = 0;
				if (!mem.ReadUseCache(pointerValueA, &vftPointerValueA, sizeof(UINT64)))
					bIsAValid = false;
				else
					bIsAValid = (vftPointerValueA != 0);
			}

			if (bCheckForVft && bIsBValid)
			{
				UINT64 vftPointerValueB = 0;
				if (!mem.ReadUseCache(pointerValueB, &vftPointerValueB, sizeof(UINT64)))
					bIsBValid = false;
				else
					bIsBValid = (vftPointerValueB != 0);
			}

			if (bIsAValid && bIsBValid)
				return j;
		}

		return -1;
	}

	template<int Alignment = 4, typename T>
	inline int32_t FindOffset(std::vector<std::pair<UINT64, T>>& ObjectValuePair, int MinOffset = 0x28, int MaxOffset = 0x1A0)
	{
		int32_t HighestFoundOffset = MinOffset;

		for (int i = 0; i < ObjectValuePair.size(); i++)
		{
			UINT64 ObjectAddress = ObjectValuePair[i].first;
			T buffer;

			for (int j = HighestFoundOffset; j < MaxOffset; j += Alignment)
			{
				if (!mem.ReadUseCache(ObjectAddress + j, &buffer, sizeof(T)))
					continue;

				if (buffer == ObjectValuePair[i].second && j >= HighestFoundOffset)
				{
					if (j > HighestFoundOffset)
					{
						HighestFoundOffset = j;
						i = 0;
					}
					break;
				}
			}
		}
		return HighestFoundOffset;
	}

}
