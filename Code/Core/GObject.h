#pragma once

#include <windows.h>
#include <string>
#include <locale>
#include <codecvt>
#include <format>
#include <vector>

#include <Memory/Memory.h>

#include <Core/Data.h>
#include <Core/Offset.h>
#include <Core/OffsetsConfig.h>
#include <Core/Process.h>
#include <Core/Wrappers.h>
#include <Core/GName.h>
#include <Core/Engine.h>
#include <Core/Enums.h>

namespace GObject
{
	struct
	{
		UINT64 ObjectArray;
		UINT64 MaxElements;
		UINT64 NumElements;
		UINT64 MaxChunks;
		UINT64 NumChunks;
	} UObjectArray;

	inline UE_UClass FindObject(const std::string& Name)
	{
		if (GameData.GObject.NameToAddress.count(Name))
		{
			return UE_UClass(GameData.GObject.NameToAddress[Name]);
		}
		return (UINT64)nullptr;
	}

	inline void DumpName()
	{
		File ObjectsDumpFile(GameData.Directory / "ObjectsDump.txt", "w");

		int ObjectsCount = 0;
		const UINT64 ElementsPerChunk = Offset.NumElementsPerChunk;
		const UINT64 ElementCount = UObjectArray.NumElements;
		const UINT64 RequiredChunks = ElementsPerChunk == 0
			? 0 : (ElementCount + ElementsPerChunk - 1) / ElementsPerChunk;
		const size_t TotalChunks = (size_t)(UObjectArray.NumChunks < RequiredChunks
			? UObjectArray.NumChunks : RequiredChunks);
		for (size_t ChunkIndex = 0; ChunkIndex < TotalChunks; ChunkIndex++)
		{
			UINT64 ObjectArrayBase = mem.ReadUseCache<UINT64>(UObjectArray.ObjectArray + ChunkIndex * 8) & ~0xFULL;

			printf("\nObjectArrayBase[%zu]: 0x%llx\n", ChunkIndex, (unsigned long long)ObjectArrayBase);
			fflush(stdout);

			// Uninitialized slots resolve to wild pointers - skip those chunks.
			// Upper bound follows the canonical x64 user-mode range (consistent
			// with the blockBase check in ResolveName). Heaps living above 2^40
			// are perfectly valid.
			const bool BaseValid = (ObjectArrayBase >= 0x10000ULL && ObjectArrayBase < 0x800000000000ULL);
			if (!BaseValid) {
				printf("[WARN] Chunk[%zu] base 0x%llX out of sane range, skip\n",
					ChunkIndex, (unsigned long long)ObjectArrayBase);
				fflush(stdout);
				continue;
			}

			const UINT64 chunkStart = (UINT64)ChunkIndex * ElementsPerChunk;
			const UINT64 remaining = ElementCount > chunkStart ? ElementCount - chunkStart : 0;
			const UINT64 ObjectCount = ElementsPerChunk < remaining ? ElementsPerChunk : remaining;

			for (size_t ObjectIndex = 0; ObjectIndex < ObjectCount; ObjectIndex++)
			{
				const UINT64 Index = ObjectIndex;

				// progress output every 10000 objects so the run does not look
				// frozen
				if ((ObjectIndex % 10000) == 0 && ObjectIndex != 0) {
					printf("\r  chunk[%zu] %zu/%llu  cumulative=%d",
						ChunkIndex, ObjectIndex, (unsigned long long)ObjectCount, ObjectsCount);
					fflush(stdout);
				}

				// Current Wardogs FUObjectItem: UObject* lives at +0x10;
				// +0x08 contains flags and the chunk pointer can carry low tag bits.
				// (Offset.UObject.ItemObject). Freed/uninitialized slots keep
				// garbage values - canonical-pointer filter before walking
				// GetFullName.
				auto ObjectItem = mem.ReadUseCache<UINT64>(ObjectArrayBase + (Index * Offset.UObject.Size) + Offset.UObject.ItemObject);
				if (ObjectItem < 0x10000ULL || ObjectItem >= 0x800000000000ULL) continue;
				UE_UObject Object(ObjectItem);

				if (Object) {
					const auto ObjIndex = Object.GetIndex();
					const auto Address = Object.GetAddress();
					const auto FullName = Object.GetFullName();

					ObjectsDumpFile.WriteFormat("[{:09d}] ({}/{}) <0x{:011X}> {}\n", ObjectsCount, ChunkIndex, ObjIndex, Address, FullName);
					ObjectsDumpFile.Flush();

					ObjectsCount++;

					GameData.GObject.NameToAddress[FullName] = Address;

					GameData.GObject.Array.push_back(Address);
				}
			}

			printf("Chunk[%zu/%zu] done, cumulative objects=%d\n",
				ChunkIndex + 1, TotalChunks, ObjectsCount);
		}

		printf("\nObjects Done. Total=%d, MapSize=%zu\n",
			ObjectsCount, GameData.GObject.NameToAddress.size());
	}

	inline void DumpPackage()
	{
		printf("\n[Package]\n");

		std::unordered_map<UINT64, std::vector<UE_UObject>> Packages;
		int Count = 0;
		for (const auto& Item : GameData.GObject.Array)
		{
			UE_UObject Object(Item);
			if (Object.IsA<UE_UStruct>() || Object.IsA<UE_UEnum>())
			{
				Count++;
				auto packageObj = Object.GetPackageObject();
				Packages[packageObj.GetAddress()].push_back(Object);
				printf("\rPackage: %d %d/%d", Packages.size(), Count, GameData.GObject.Array.size());
			}
		}

		printf("\n");

		UINT32 Index = 1;
		UINT32 Saved = 0;

		auto path = GameData.Directory / "DUMP";
		std::filesystem::create_directories(path);

		for (UE_UPackage Package : Packages) {
			printf("\rProcessing: %d/%d", Index++, Packages.size());

			Package.Process();
			if (Package.Save(path)) { Saved++; }
		}

		printf("\nSaved packages: %d", Saved);

		printf("\n");
	}

	inline void FindUStructAllOffset()
	{
		printf("\n[UEStructOffset]\n");

		// UStruct
		{
			// SuperStruct
			{
				std::vector<std::pair<UINT64, UINT64>> Infos;
				Infos.push_back({ FindObject("Class CoreUObject.Struct").GetAddress(), FindObject("Class CoreUObject.Field").GetAddress() });
				Infos.push_back({ FindObject("Class CoreUObject.Class").GetAddress(), FindObject("Class CoreUObject.Struct").GetAddress() });

				Offset.UStruct.SuperStruct = OffsetUtils::FindOffset(Infos);

				printf("Offset.UStruct.SuperStruct: 0x%01X\n", Offset.UStruct.SuperStruct);
			}

			// Children
			{
				std::vector<std::pair<UINT64, UINT64>> Infos;
				Infos.push_back({ FindObject("Class Engine.PlayerController").GetAddress(), FindObject("Function Engine.PlayerController.WasInputKeyJustReleased").GetAddress() });
				Infos.push_back({ FindObject("Class Engine.Controller").GetAddress(), FindObject("Function Engine.Controller.UnPossess").GetAddress() });

				Offset.UStruct.Children = OffsetUtils::FindOffset(Infos);

				printf("Offset.UStruct.Children: 0x%01X\n", Offset.UStruct.Children);
			}

			// ChildPropertie
			{
				const auto ObjA = FindObject("ScriptStruct CoreUObject.Color").GetAddress();
				const auto ObjB = FindObject("ScriptStruct CoreUObject.Guid").GetAddress();

				Offset.UStruct.ChildProperties = OffsetUtils::GetValidPointerOffset(ObjA, ObjB, Offset.UStruct.Children + 0x08, 0x80);

				printf("Offset.UStruct.ChildProperties: 0x%01X\n", Offset.UStruct.ChildProperties);
			}

			// PropertiesSize
			{
				std::vector<std::pair<UINT64, INT32>> Infos;
				Infos.push_back({ FindObject("ScriptStruct CoreUObject.Color").GetAddress(), 0x04 });
				Infos.push_back({ FindObject("ScriptStruct CoreUObject.Guid").GetAddress(), 0x10 });

				Offset.UStruct.PropertiesSize = OffsetUtils::FindOffset(Infos);

				printf("Offset.UStruct.PropertiesSize: 0x%01X\n", Offset.UStruct.PropertiesSize);
			}

			// MinAlignemnt
			{
				std::vector<std::pair<UINT64, INT32>> Infos;
				Infos.push_back({ FindObject("ScriptStruct CoreUObject.Transform").GetAddress(), 0x10 });
				Infos.push_back({ FindObject("Class Engine.PlayerController").GetAddress(), 0x8 });

				Offset.UStruct.MinAlignemnt = OffsetUtils::FindOffset(Infos);

				printf("Offset.UStruct.MinAlignemnt: 0x%01X\n", Offset.UStruct.MinAlignemnt);
			}
		}

		// UField
		{
			// Next
			{
				const auto KismetSystemLibraryChild = FindObject("Class Engine.KismetSystemLibrary").GetChildren().GetAddress();
				const auto KismetStringLibraryChild = FindObject("Class Engine.KismetStringLibrary").GetChildren().GetAddress();

#undef max
				const auto HighestUObjectOffset = std::max({ Offset.UObject.Index, Offset.UObject.Name, Offset.UObject.Flags, Offset.UObject.Outer, Offset.UObject.Class });
#define max(a,b)            (((a) > (b)) ? (a) : (b))

				Offset.UField.Next = OffsetUtils::GetValidPointerOffset(KismetSystemLibraryChild, KismetStringLibraryChild, OffsetUtils::Align(HighestUObjectOffset + 0x4, 0x8), 0x60);
				printf("Offset.UField.Next: 0x%01X (HighestUObjectOffset: 0x%01X)\n", Offset.UField.Next, HighestUObjectOffset);
			}
		}

		// UEnum
		{
			// Names
			{
				std::vector<std::pair<UINT64, INT32>> EnumNames;
				EnumNames.push_back({ FindObject("Enum Engine.ENetRole").GetAddress(), 0x5 });
				EnumNames.push_back({ FindObject("Enum Engine.ETraceTypeQuery").GetAddress(), 0x5 });

				Offset.UEnum.Names = OffsetUtils::FindOffset(EnumNames) - 0x8;

				printf("Offset.UEnum.Names: 0x%01X\n", Offset.UEnum.Names);
			}
		}

		// UFunction
		{
			// FunctionFlags
			{
				std::vector<std::pair<UINT64, EFunctionFlags>> Infos;
				Infos.push_back({ FindObject("Function Engine.PlayerController.WasInputKeyJustPressed").GetAddress(), EFunctionFlags::Final | EFunctionFlags::Native | EFunctionFlags::Public | EFunctionFlags::BlueprintCallable | EFunctionFlags::BlueprintPure | EFunctionFlags::Const });
				Infos.push_back({ FindObject("Function Engine.PlayerController.ToggleSpeaking").GetAddress(), EFunctionFlags::Exec | EFunctionFlags::Native | EFunctionFlags::Public });
				Infos.push_back({ FindObject("Function Engine.PlayerController.FOV").GetAddress(), EFunctionFlags::Exec | EFunctionFlags::Native | EFunctionFlags::Public });

				Offset.UFunction.FunctionFlags = OffsetUtils::FindOffset(Infos);

				printf("Offset.UFunction.FunctionFlags: 0x%01X\n", Offset.UFunction.FunctionFlags);
			}

			// Func
			{
				auto WasInputKeyJustPressed = FindObject("Function Engine.PlayerController.WasInputKeyJustPressed").GetAddress();
				auto ToggleSpeaking = FindObject("Function Engine.PlayerController.ToggleSpeaking").GetAddress();
				auto SwitchLevel = FindObject("Function Engine.PlayerController.SwitchLevel").GetAddress();

				for (int i = 0x40; i < 0x140; i += 8)
				{
					UINT64 WasInputKeyJustPressedAddr = 0;
					UINT64 ToggleSpeakingAddr = 0;
					UINT64 SwitchLevelAddr = 0;

					mem.ReadUseCache((WasInputKeyJustPressed + i), &WasInputKeyJustPressedAddr, sizeof(UINT64));
					mem.ReadUseCache((ToggleSpeaking + i), &ToggleSpeakingAddr, sizeof(UINT64));
					mem.ReadUseCache((SwitchLevel + i), &SwitchLevelAddr, sizeof(UINT64));

					if (OffsetUtils::IsInProcessRange(WasInputKeyJustPressedAddr) &&
						OffsetUtils::IsInProcessRange(ToggleSpeakingAddr) &&
						OffsetUtils::IsInProcessRange(SwitchLevelAddr))
					{
						Offset.UFunction.Func = i;

						printf("Offset.UFunction.Func: 0x%01X\n", Offset.UFunction.Func);
						break;
					}

				}
			}
		}

		// FField
		{
			// Class - stock UE5: ClassPrivate@0x8; uses the default value or
			// the config.ini override
			{
				printf("Offset.FField.Class: 0x%01X\n", Offset.FField.Class);
			}

			// Next
			{
				const auto GuidChildren = FindObject("ScriptStruct CoreUObject.Guid").GetChildProperties().GetAddress();
				const auto VectorChildren = FindObject("ScriptStruct CoreUObject.Vector").GetChildProperties().GetAddress();
				Offset.FField.Next = OffsetUtils::GetValidPointerOffset(GuidChildren, VectorChildren, Offset.FField.Owner + 0x8, 0x48);
				printf("Offset.FField.Next: 0x%01X\n", Offset.FField.Next);
			}

			// Name
			{
				UE_FField GuidChild = FindObject("ScriptStruct CoreUObject.Guid").GetChildProperties();
				UE_FField VectorChild = FindObject("ScriptStruct CoreUObject.Vector").GetChildProperties();

				std::string GuidChildName = GuidChild.GetName();
				std::string VectorChildName = VectorChild.GetName();

				if ((GuidChildName == "A" || GuidChildName == "D") && (VectorChildName == "X" || VectorChildName == "Z"))
				{
					printf("Offset.FField.Name: 0x%01X\n", Offset.FField.Name);
				}
				else {
					// Scan 0x20..0x80: Wardogs shrinks FFieldVariant to 8 bytes,
					// putting Next at 0x18 and Name at 0x20. The old scan start
					// of 0x28 (with an upper bound of 0x40) always skipped the
					// real location - the root cause of "correct field counts
					// and types but garbage names" in early dumps.
					bool bFound = false;
					for (UINT16 cand = 0x20; cand < 0x80; cand += 4)
					{
						Offset.FField.Name = cand;
						GuidChildName = GuidChild.GetName();
						VectorChildName = VectorChild.GetName();

						if ((GuidChildName == "A" || GuidChildName == "D") && (VectorChildName == "X" || VectorChildName == "Z"))
						{
							printf("Offset.FField.Name: 0x%01X (probed)\n", Offset.FField.Name);
							bFound = true;
							break;
						}
					}
					if (!bFound) {
						printf("[WARN] FField.Name probe failed (no hit in 0x20..0x80), current value 0x%X\n", (unsigned)Offset.FField.Name);
					}
				}
			}

			//Flags
			{
				// FName is 8 bytes {u32 id, u32 number}; Flags follows Name
				Offset.FField.Flags = Offset.FField.Name + 8;
				printf("Offset.FField.Flags: 0x%01X\n", Offset.FField.Flags);
			}
		}

		// UProperty
		{
			// ElementSize
			{
				std::vector<std::pair<UINT64, INT32>> Infos;

				UE_UStruct Guid = FindObject("ScriptStruct CoreUObject.Guid");
				Infos.push_back({ Guid.FindMember("A").GetAddress(), 0x04 });
				Infos.push_back({ Guid.FindMember("C").GetAddress(), 0x04 });
				Infos.push_back({ Guid.FindMember("D").GetAddress(), 0x04 });
				Offset.UProperty.ElementSize = OffsetUtils::FindOffset(Infos);

				printf("Offset.UProperty.ElementSize: 0x%01X\n", Offset.UProperty.ElementSize);
			}

			// ArrayDim
			{
				std::vector<std::pair<UINT64, INT32>> Infos;

				UE_UStruct Guid = FindObject("ScriptStruct CoreUObject.Guid");
				Infos.push_back({ Guid.FindMember("A").GetAddress(), 0x01 });
				Infos.push_back({ Guid.FindMember("C").GetAddress(), 0x01 });
				Infos.push_back({ Guid.FindMember("D").GetAddress(), 0x01 });

				Offset.UProperty.ArrayDim = OffsetUtils::FindOffset(Infos);

				printf("Offset.UProperty.ArrayDim: 0x%01X\n", Offset.UProperty.ArrayDim);
			}

			// Offset
			{
				std::vector<std::pair<UINT64, INT32>> Infos;

				UE_UStruct Color = FindObject("ScriptStruct CoreUObject.Color");
				Infos.push_back({ Color.FindMember("B").GetAddress(), 0x00 });
				Infos.push_back({ Color.FindMember("G").GetAddress(), 0x01 });
				Infos.push_back({ Color.FindMember("R").GetAddress(), 0x02 });

				if (Infos[2].first == (UINT64)nullptr) [[unlikely]]
					Infos[2].first = Color.FindMember("r").GetAddress();

				Offset.UProperty.Offset = OffsetUtils::FindOffset(Infos);

				printf("Offset.UProperty.Offset: 0x%01X\n", Offset.UProperty.Offset);
			}

			// Size
			{
				std::vector<std::pair<UINT64, uint8_t>> Infos;

				UE_UStruct Engine = FindObject("Class Engine.Engine");
				auto m1 = Engine.FindMember("bIsOverridingSelectedColor");
				auto m2 = Engine.FindMember("bEnableOnScreenDebugMessagesDisplay");
				auto m3 = FindObject("Class Engine.PlayerController").FindMember("bAutoManageActiveCameraTarget");
				if (m1.GetAddress()) Infos.push_back({ m1.GetAddress(), 0xFF });
				if (m2.GetAddress()) Infos.push_back({ m2.GetAddress(), 0b00000010 });
				if (m3.GetAddress()) Infos.push_back({ m3.GetAddress(), 0xFF });
				Offset.UProperty.Size = OffsetUtils::FindOffset<1>(Infos, Offset.UProperty.Size) - 0x4;

				// the old "DebugProperties -> Size += 8" heuristic was removed:
				// subclass-field reads now use the dedicated PropertyClass offset
				printf("Offset.UProperty.Size: 0x%01X\n", Offset.UProperty.Size);
			}

			// PropertyClass: start of the FProperty subclass field area (the
			// base for all type-pointer reads). Anchor: RootComponent of
			// Class Engine.Actor is an ObjectProperty whose type must be
			// "SceneComponent".
			{
				auto RootComp = FindObject("Class Engine.Actor").FindMember("RootComponent");
				bool bFoundPC = false;
				if (RootComp) {
					for (UINT16 cand = 0x60; cand < 0xA0; cand += 8)
					{
						UINT64 p = mem.ReadUseCache<UINT64>(RootComp.GetAddress() + cand);
						if (p >= 0x10000ULL && p < 0x800000000000ULL)
						{
							UINT32 nid = mem.ReadUseCache<UINT32>(p + Offset.UObject.Name);
							if (GName::ResolveName(nid) == "SceneComponent")
							{
								Offset.UProperty.PropertyClass = cand;
								bFoundPC = true;
								break;
							}
						}
					}
				}
				printf("Offset.UProperty.PropertyClass: 0x%01X%s\n",
					(unsigned)Offset.UProperty.PropertyClass, bFoundPC ? " (probed)" : " [probe failed, using default]");
			}
		}

		// Self-check: the first children of ScriptStruct CoreUObject.Guid must
		// be named A/B - otherwise name resolution is still broken and the
		// generated SDK member names would be garbage.
		{
			UE_FField First = FindObject("ScriptStruct CoreUObject.Guid").GetChildProperties();
			std::string N1 = First.GetName();
			UE_FField Second = First.GetNext();
			std::string N2 = Second.GetName();
			printf("[SelfCheck] Guid children: %s, %s (expected A/B)\n", N1.c_str(), N2.c_str());
			if (!(N1 == "A" && N2 == "B")) {
				printf("[WARN][SelfCheck] FField name resolution is still broken, SDK member names will be unreliable!\n");
			}
		}

		// Priority fix: re-apply config.ini overrides AFTER auto-detection so
		// manual values always win over probe results.
		OffsetsConfig::Apply();
	}

	inline void Init()
	{
		// Stock UE5 FChunkedFixedUObjectArray layout (header fields after
		// GObjects):
		//   +0x00 Objects (chunk table pointer), +0x10 MaxElements,
		//   +0x14 NumElements, +0x18 MaxChunks, +0x1C NumChunks
		// (every field offset can be overridden via config.ini GUObjectArray.*)
		UObjectArray.ObjectArray = mem.Read<UINT64>(GameData.Global.Base + Offset.GObjects + 0x0);
		UObjectArray.MaxElements = mem.Read<UINT32>(GameData.Global.Base + Offset.GObjects + Offset.GUObjectArray.MaxElements);
		UObjectArray.NumElements = mem.Read<UINT32>(GameData.Global.Base + Offset.GObjects + Offset.GUObjectArray.NumElements);
		UObjectArray.MaxChunks = mem.Read<UINT32>(GameData.Global.Base + Offset.GObjects + Offset.GUObjectArray.MaxChunks);
		UObjectArray.NumChunks = mem.Read<UINT32>(GameData.Global.Base + Offset.GObjects + Offset.GUObjectArray.NumChunks);

		printf("\n[GObject]\n");

		printf("ObjectArray: 0x%llX\n", UObjectArray.ObjectArray);
		printf("MaxElements: %llu\n", UObjectArray.MaxElements);
		printf("NumElements: %llu\n", UObjectArray.NumElements);
		printf("MaxChunks: %llu\n", UObjectArray.MaxChunks);
		printf("NumChunks: %llu\n", UObjectArray.NumChunks);

		DumpName();

		FindUStructAllOffset();

		DumpPackage();
	}
}
