# Porting UEDumper to a different target

Everything below happens in `config.ini` plus one line in `Code/Core/Data.h`.
No recompile is needed for offset changes.

## 0. Prerequisites

```powershell
powershell -ExecutionPolicy Bypass -File scripts/Fetch-MemProcFS.ps1
msbuild UEDumper.sln -p:Configuration=Release -p:Platform=x64
```

## 1. Point at the process

`Code/Core/Data.h`:

```cpp
std::string Name = "YourGame-Win64-Shipping";
```

This is the only recompile-forcing change. `[DMA] DMA.Pid` can override the
selection at runtime when several matching processes exist.

## 2. Get a module copy

Run with `SafeMode = 0`, `SkipAOB = 1`, `Backend = rpm` (or `dma`). The tool
copies the module into a local buffer so the AOB scan can run without touching
the live process again.

## 3. Find the globals

Set `SkipAOB = 0` and let the signatures resolve `GObjects` / `GNames` /
`GWorld` / `GEngine`. The log prints each resolved RVA. If a signature misses
you get `[WARN] AOB not found` — resolve it by hand in IDA and paste the RVA
into `[Offsets]` instead. The resolution formula is:

```
RVA = match + DispOffset + 4 + sign_extend(disp32) - ImageBase
```

## 4. Verify the name pool before anything else

This is the step that catches most mistakes. Set:

```ini
SafeMode = 1
SkipAOB = 1
```

and check the ten printed UObject names. Good output looks like:

```
0 | /Script/CoreUObject
1 | Object
2 | Field
3 | Struct
4 | Class
5 | Function
6 | Enum
7 | Package
8 | Property
9 | Actor
```

Anything else means the layout is wrong — see the table below.

| Symptom | Likely cause |
|---|---|
| All names `FAIL` | `GNames` wrong, or `NamePoolBlocksOffset` off by 8/0x10 |
| Names are garbage text | `NameEntryStride` / `NameHeaderOffset` / `NameDataOffset` wrong |
| First name is not `None` | `ChunkMask` / `NameMask` wrong, or `GNames` points at the wrong structure |
| `chunk0 pointer is 0` | `GObjects` wrong, or the `FUObjectArray` header offsets differ |
| `GObjects resolved to an invalid pointer` | RVAs are stale for this build |
| Names truncate mid-string | `NameFixedKind` / `NameFixedKindLen` wrong for this engine |

### Stock UE5 vs modified engine

These are the values that differ most between builds. Stock UE5 defaults:

| Key | Stock UE5 | This repo's example build |
|---|---|---|
| `NameEntryStride` | `2` | `8` |
| `NameHeaderOffset` | `0x0` | `0x8` |
| `NameDataOffset` | `0x2` | `0xC` |
| `NamePoolBlocksOffset` | `0x10` | `0x20` (see note) |
| `NameFixedKind` | `0` (disabled) | `2` |
| `UObject.ItemObject` | `0x0` | `0x10` |
| `FField.Next` | `0x20` | `0x18` |
| `FField.Name` | `0x28` | `0x20` |

`GNames` and `NamePoolBlocksOffset` are coupled: an anchor pointing at the
pool object needs `+0x20`, one pointing at the blocks table needs `+0x10`.
Both address the same table, so a mismatch reads the wrong pointers.

## 5. Confirm the object array

`UObject.ItemObject` is the `UObject*` offset inside `FUObjectItem`, and
`UObject.Size` is the `FUObjectItem` stride. With `UObject.Size = 0x18`:

| `ItemObject` | Layout |
|---|---|
| `0x0` | stock UE5 (`UObject*` first) |
| `0x8` | flags at `+0x0`, object at `+0x8` |
| `0x10` | flags at `+0x8`, object at `+0x10` |

The chunk table pointer can carry low tag bits; `GObject.h` and `Main.cpp`
mask with `& ~0xFULL`. Keep that when porting.

## 6. Full dump

Once the ten names are right:

```ini
SafeMode = 0
SkipAOB = 0        ; optionally, if you want signatures to resolve the RVAs
```

Output lands in `Games/<ProcessName>/`.

## Notes on this repository's offsets

The RVAs and layouts shipped in `config.ini` and `Code/Core/Offset.h` are
**example values for one particular client build**. They are not universal and
carry no guarantee for any other build, including later revisions of the same
title. Treat them as a worked example of a modified UE5 layout, not as a
supported configuration.
