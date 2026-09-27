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

// AOB offset resolution + SDK key-offset output.
// Signatures and disp offsets come from the [AOB] section of config.ini
// (editable after game updates without recompiling); numeric keys in
// [Offsets] are applied afterwards and always win.
// Values and signatures are maintained in config.ini; the shipped manual RVA
// set is a worked example of a modified UE5 layout, not a universal default.

namespace DumpOffset
{
	inline void print4(const char* var_name, uint32_t offset)
	{
		printf("constexpr UINT32 %s = 0x%04X;\n", var_name, offset);
	}

	inline void print8(const char* var_name, uint32_t offset)
	{
		printf("constexpr UINT32 %s = 0x%08X;\n", var_name, offset);
	}

	inline void print4(const char* var_name, uint8_t offset)
	{
		printf("constexpr UINT32 %s = 0x%01X;\n", var_name, offset);
	}

	inline void print4(const char* var_name, uint64_t offset)
	{
		printf("constexpr UINT32 %s = 0x%016llX;\n", var_name, offset);
	}

	// Resolve the rip-relative target for the first maxCheck hits of a
	// signature and return the most frequent RVA.
	// Mode deduplication: the same engine code gets instantiated many times
	// (the GObjects signature matches 100+ call sites) and all hits must
	// resolve to the same global; a split result means the signature is bad.
	inline UINT64 ResolveAobMode(const char* sig, int dispOffset, int maxCheck = 8)
	{
		auto Results = Process::FindSignatureAll(sig);

		std::unordered_map<UINT64, int> Tally;
		UINT64 Best = 0;
		int BestCount = 0;

		int n = (int)Results.size();
		if (n > maxCheck) n = maxCheck;

		for (int i = 0; i < n; i++)
		{
			UINT64 rva = Process::CalcRelative(Results[i] + dispOffset);
			if (!rva) continue;

			int c = ++Tally[rva];
			if (c > BestCount)
			{
				BestCount = c;
				Best = rva;
			}
		}
		return Best;
	}

	// Read "AOB.<Name>.Sig" + "AOB.<Name>.DispOffset" from config.ini and
	// resolve. Returns 0 (caller keeps the current value) when the signature
	// key is missing; DispOffset falls back to the built-in default.
	inline UINT64 ResolveFromConfig(const char* Name, int defaultDispOffset)
	{
		std::string sigKey = std::string("AOB.") + Name + ".Sig";
		std::string sig = OffsetsConfig::GetString(sigKey, "");
		if (sig.empty())
		{
			printf("[AOB] config.ini is missing %s, skipping\n", sigKey.c_str());
			return 0;
		}

		std::string dispKey = std::string("AOB.") + Name + ".DispOffset";
		UINT64 disp = OffsetsConfig::GetUInt(dispKey, (UINT64)defaultDispOffset);

		return ResolveAobMode(sig.c_str(), (int)disp);
	}

	inline void Init()
	{
		printf("\n[Dump Offset]\n");

		// load config.ini at startup (manual overrides when AOB breaks)
		OffsetsConfig::Load();

		// SkipAOB=1: skip the signature scan (recommended after game updates
		// until the signatures have been re-verified; uses ini/defaults only)
		if (OffsetsConfig::GetBool("SkipAOB", true))
		{
			printf("[Dump Offset] SkipAOB=1, skipping AOB scan, using config.ini / built-in defaults only\n");
			OffsetsConfig::Apply();
			printf("\n");
			return;
		}

		// ---- Wardogs global resolution (signatures live in config.ini [AOB]) ----
		{
			auto r = ResolveFromConfig("GObjects", 0x18);
			if (r) { Offset.GObjects = r; print8("GObjects", (UINT32)r); }
		}
		{
			// NumElements shares the GObjects signature (leading cmp); the
			// resolved value must always equal GObjects+0x14 - sanity check
			auto r = ResolveFromConfig("NumElements", 0x2);
			if (r) print8("NumElements", (UINT32)r);
		}
		{
			auto r = ResolveFromConfig("GNames", 0x3);
			if (r) { Offset.GNames = r; print8("GNames", (UINT32)r); }
		}
		{
			auto r = ResolveFromConfig("GWorld", 0x15);
			if (r) { Offset.GWorld = r; print8("GWorld", (UINT32)r); }
		}
		{
			auto r = ResolveFromConfig("GEngine", 0x3);
			if (r) { Offset.GEngine = r; print8("GEngine", (UINT32)r); }
		}

		printf("\n");

		// apply numeric overrides from config.ini after the AOB scan
		// (manual values always win over scan results)
		OffsetsConfig::Apply();

		printf("\n");
	}

	inline void SDK()
	{
		printf("\n[SDK Dump Offset]\n");

		// basic engine components
		print4("GameViewport", GameData.Offset.NameToOffset["Engine.Engine.GameViewport"]);
		print4("World", GameData.Offset.NameToOffset["Engine.GameViewportClient.World"]);
		print4("OwningGameInstance", GameData.Offset.NameToOffset["Engine.World.OwningGameInstance"]);
		print4("LocalPlayers", GameData.Offset.NameToOffset["Engine.GameInstance.LocalPlayers"]);
		// networking
		print4("NetDriver", GameData.Offset.NameToOffset["Engine.World.NetDriver"]);
		print4("NetDriverLevels", GameData.Offset.NameToOffset["Engine.NetDriver.Time"] + 0x308);
		// levels
		print4("PersistentLevel", GameData.Offset.NameToOffset["Engine.World.PersistentLevel"]);
		print4("Levels", GameData.Offset.NameToOffset["Engine.World.Levels"]);
		printf("\n");

		// game state
		print4("GameState", GameData.Offset.NameToOffset["Engine.World.GameState"]);
		print4("PlayerArray", GameData.Offset.NameToOffset["Engine.GameStateBase.PlayerArray"]);
		printf("\n");

		// player controller
		print4("PlayerController", GameData.Offset.NameToOffset["Engine.Player.PlayerController"]);
		print4("ControlRotation", GameData.Offset.NameToOffset["Engine.Controller.ControlRotation"]);
		print4("bShowMouseCursor", GameData.Offset.NameToOffset["Engine.PlayerController.bShowMouseCursor"]);
		print4("AcknowledgedPawn", GameData.Offset.NameToOffset["Engine.PlayerController.AcknowledgedPawn"]);
		printf("\n");

		// UI
		print4("MyHUD", GameData.Offset.NameToOffset["Engine.PlayerController.MyHUD"]);
		print4("Visibility", GameData.Offset.NameToOffset["UMG.Widget.Visibility"]);
		printf("\n");

		// camera
		print4("PlayerCameraManager", GameData.Offset.NameToOffset["Engine.PlayerController.PlayerCameraManager"]);
		print4("ViewTarget", GameData.Offset.NameToOffset["Engine.PlayerCameraManager.ViewTarget"]);
		print4("CameraCachePrivate", GameData.Offset.NameToOffset["Engine.PlayerCameraManager.CameraCachePrivate"]);
		print4("DefaultFOV", GameData.Offset.NameToOffset["Engine.PlayerCameraManager.DefaultFOV"]);
		printf("\n");

		// actor components
		print4("bActorIsBeingDestroyed", GameData.Offset.NameToOffset["Engine.Actor.bActorIsBeingDestroyed"]);
		print4("RootComponent", GameData.Offset.NameToOffset["Engine.Actor.RootComponent"]);
		print4("ComponentVelocity", GameData.Offset.NameToOffset["Engine.SceneComponent.ComponentVelocity"]);
		print4("Mesh", GameData.Offset.NameToOffset["Engine.CHARACTER.Mesh"]);
		print4("BoneArray", GameData.Offset.NameToOffset["Engine.SkinnedMeshComponent.MasterPoseComponent"] + 8);
		print4("SkeletalMesh", GameData.Offset.NameToOffset["Engine.SkinnedMeshComponent.SkeletalMesh"]);
		print4("bAlwaysCreatePhysicsState", GameData.Offset.NameToOffset["Engine.PrimitiveComponent.bAlwaysCreatePhysicsState"]);
		print4("AllBlueprintCreatedComponents", GameData.Offset.NameToOffset["Engine.Actor.BlueprintCreatedComponents"] - 0x60);
		printf("\n");

		// animation
		print4("AnimScriptInstance", GameData.Offset.NameToOffset["Engine.SkeletalMeshComponent.AnimScriptInstance"]);
		printf("\n");

		// player state
		print4("PlayerState", GameData.Offset.NameToOffset["Engine.Pawn.PlayerState"]);
		print4("PlayerNamePrivate", GameData.Offset.NameToOffset["Engine.PlayerState.PlayerNamePrivate"]);
		printf("\n");
	}
}
