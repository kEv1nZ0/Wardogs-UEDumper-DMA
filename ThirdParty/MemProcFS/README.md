# Third-party DMA runtime

The DMA backend loads these components at runtime with `LoadLibraryExW` +
`GetProcAddress`. There is **no link-time dependency** (no `.lib`, no
`#pragma comment(lib)`), but `Memory/DmaBackend.cpp` does include
`vmmdll.h` for the export declarations, so the header is required to compile.

**They are not distributed with this repository.** Fetch them with:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/Fetch-MemProcFS.ps1
```

which populates:

```
ThirdParty/MemProcFS/include/vmmdll.h       <- MemProcFS release
ThirdParty/MemProcFS/include/leechcore.h    <- MemProcFS release
ThirdParty/MemProcFS/bin/vmm.dll            <- MemProcFS release
ThirdParty/MemProcFS/bin/leechcore.dll      <- MemProcFS release
ThirdParty/MemProcFS/bin/FTD3XX.dll         <- MemProcFS release / FTDI
```

All five files are required; the build fails if any is missing. `vmm.dll`,
`leechcore.dll` and `FTD3XX.dll` must sit next to `UEDumper.exe` at runtime.

## Licenses

These components keep their own upstream terms, independently of UEDumper's
MIT license. This matters if you redistribute a build that bundles them.

| Component | License | Upstream |
|---|---|---|
| MemProcFS (`vmm.dll`, `vmmdll.h`) | **AGPL-3.0** | https://github.com/ufrisk/MemProcFS |
| LeechCore (`leechcore.dll`, `leechcore.h`) | **GPL-3.0** | https://github.com/ufrisk/LeechCore |
| FTD3XX.dll | FTDI proprietary, redistribution not permitted | https://ftdichip.com/drivers/d3xx-drivers/ |

MemProcFS's README states: *"The project source code is released under: GNU
Affero General Public License v3.0. Some bundled dependencies and plugins are
released under GPLv3. ... Alternative licensing may be possible upon request."*

Because AGPL-3.0 §5(c) requires the entire combined work to be licensed under
the AGPL, **a binary redistribution that bundles these DLLs together with
UEDumper is an AGPL-3.0 combined distribution**, not an MIT one. Keeping the
files out of this repository is what allows UEDumper's own source to stay MIT.

The `rpm` backend (`Memory/RpmBackend.cpp`) has no third-party dependency at
all and is MIT-only.

## FTD3XX.dll is required

`FTD3XX.dll` is needed for the PCILeech FPGA device, and it is **also a hard
dependency for every other device**: `leechcore.dll` imports it directly, so
without `FTD3XX.dll` present the loader fails on `leechcore.dll` itself with
`Windows error 126` and the `dma` backend cannot initialize at all. MemProcFS
ships `FTD3XXWU.dll` (a WinUSB variant), which is *not* the module LeechCore
loads.

`Fetch-MemProcFS.ps1` therefore always installs it. If it is missing, take it
from the FTDI D3XX driver package (https://ftdichip.com/drivers/d3xx-drivers/)
and place it in `ThirdParty/MemProcFS/bin/`.

## Implementation notes

The backend reuses the upstream device URI, VMM process and module APIs, the
initial cache refresh and uncached virtual reads. It does not use MemProcFS's
keyboard, registry or shellcode facilities, and it does not run a `FixCr3`
routine — module base and size come from the VMM module map as-is.
