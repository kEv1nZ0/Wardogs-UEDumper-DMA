# AGENTS.md

## Repo shape

- Visual Studio solution `UEDumper.sln` -> `UEDumper/UEDumper.vcxproj` (toolset v143, C++20, `/utf-8`).
- Entry point: `UEDumper/Main.cpp`.
- Dump / UE parsing logic: `Code/Core/`.
- Memory layer: `Memory/` - `IBackend.h` (extension interface), `DmaBackend.{h,cpp}` (MemProcFS implementation), `RpmBackend.{h,cpp}` (ReadProcessMemory), `Memory.{h,cpp}` (facade with backend registry + 4 KB page cache).
- No kernel driver is bundled or referenced anywhere in this project. Custom backends are user-provided implementations of `IBackend` registered via `Memory::RegisterBackend`.
- Third-party DMA runtime under `ThirdParty/MemProcFS/` is **not committed** - fetch it with `scripts/Fetch-MemProcFS.ps1`.

## Conventions

- Comments and console output are English only (the project is public).
- The target process name lives in `Code/Core/Data.h`. The default offsets in `Code/Core/Offset.h` and `config.ini` are example values for one specific client build; they are overridable and expected to be re-resolved after an update.
- Resolution order: built-in defaults -> `[AOB]` scan results -> `[Offsets]` numeric keys (manual values always win).

## Build and run

- Fetch the DMA runtime first: `powershell -ExecutionPolicy Bypass -File scripts/Fetch-MemProcFS.ps1`.
- Build via `UEDumper.sln` (not the bare vcxproj - `$(SolutionDir)` include paths break otherwise). `Release|x64` is the meaningful configuration.
- Builds output to `x64/Release-DMA/` or `x64/Debug-DMA/` and deploy whichever DMA DLLs are present. The build seeds `config.ini` only if missing and always updates `config.example.ini`; runtime loads config beside the exe.
- No admin manifest: the `rpm` backend works unelevated on same-session processes. Driver-backed forks add their own elevation as needed.
- Regression tests: `Tests/Run.ps1` uses an isolated mock VMM DLL and also checks local RPM reads. Hardware verification bar: build + SafeMode run showing 10 plausible real UObject names; mock names do not validate live offsets.

## Gotchas

- `Process::FindSignatureAll()` returns a placeholder (buffer start) when a signature misses; treat "[WARN] AOB not found" as a real failure when debugging.
- Large reads retry failed chunks by page and zero-fill unreadable pages (PE images contain unmapped pages). Majority-unreadable copies fail; failed cache pages are not retained.
- All Outer/Super/FField chain walks carry step guards (torn reads of a live process can produce pointer cycles); keep them when refactoring.
- Sanity checks on user-mode pointers use the canonical range `0x10000 .. 0x7FFFFFFFFFFF`; heaps above 2^40 are valid (this bit a previous fork).
- `GNames` and `NamePoolBlocksOffset` are a coupled pair: an anchor that points at the pool object needs `+0x20`, one that points at the blocks table needs `+0x10`. Changing one without the other silently reads the wrong table.

## Never commit

- `live-namepool.bin`, `NamesDump.txt`, `ObjectsDump.txt` and any `.partial` - these contain verbatim strings from a live target process.
- Anything under `ThirdParty/MemProcFS/bin/` or the vendored headers (AGPL-3.0 / GPL-3.0 / FTDI proprietary).
