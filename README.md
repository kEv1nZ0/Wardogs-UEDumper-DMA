# UEDumper

Unreal Engine SDK 与偏移转储工具，Windows x64。默认走 MemProcFS/LeechCore 的 DMA 通道读写目标机内存，也可以切换到本机 ReadProcessMemory。

仅供已授权的安全研究与互操作性分析使用。使用者须自行确认对目标系统拥有合法授权。

## 特性

- 两种内存后端：DMA（默认 `fpga://algo=0`）与本机 `rpm`。自定义后端实现 `IBackend` 后在 `Memory::RegisterBackend` 注册即可。
- 签名和偏移都在 `config.ini` 里，游戏更新后改配置就行，不用重编译。
- FName 池按分配器游标逐块枚举，不做整池盲扫。
- 输出 `NamesDump.txt`、`ObjectsDump.txt` 和按包拆分的 SDK 头文件，落在 `Games/<进程名>/DUMP/`。
- 安全模式先只解析前 10 个 UObject 名称，偏移不对能立刻发现，不必等全量跑完。

## 构建

需要 Visual Studio 2022（v143）、C++20，仅支持 x64。

第三方 DMA 运行时不在仓库里，先拉下来：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/Fetch-MemProcFS.ps1
```

然后构建：

```
msbuild UEDumper.sln -p:Configuration=Release -p:Platform=x64
```

请用 `UEDumper.sln` 构建，直接打开 `.vcxproj` 会因为 `$(SolutionDir)` 包含路径失效而编译不过。

产物在 `x64/Release-DMA/`（Debug 是 `x64/Debug-DMA/`）。缺任何一个第三方文件构建都会失败，所以拿到产物时三个 DLL 已经就位。首次构建生成 `config.ini`，之后每次构建只刷新 `config.example.ini`，不会覆盖你的配置。

## 使用

1. 目标机启动游戏。
2. 把 DMA 设备接到主机。
3. 在主机上运行 `x64/Release-DMA/UEDumper.exe`，DMA DLL 和 `config.ini` 放在一起，关闭其他占用同一设备的程序。
4. 首次运行保持 `SafeMode = 1`，看它打印的前 10 个 UObject 名称。出现 `/Script/CoreUObject`、`Object` 这类名称说明偏移是对的。
5. 确认无误后设 `SafeMode = 0` 跑全量，依次是模块拷贝、AOB、FName、GObjects、SDK 生成。

放脚本里跑时加 `--no-pause`，否则结束时会停在 "press any key"。

## 配置

配置放在可执行文件旁的 `config.ini`：

```ini
[General]
SafeMode = 1          ; 1 = 只校验前 10 个 UObject 名称
SkipAOB  = 1          ; 1 = 跳过签名扫描，只用 [Offsets] 与内置默认值
Backend  = dma        ; dma | rpm

[DMA]
DMA.Device = fpga://algo=0
DMA.MemMap = none     ; none | auto | 映射文件路径
DMA.Debug  = 0        ; 1 = MemProcFS 诊断输出
DMA.Pid    = 0        ; 0 = 按完整进程名匹配

[AOB]                 ; 密钥签名（更新期 fallback）
AOB.GNames.Sig        = 48 8D 0D ? ? ? ? 0F 10 00 0F 11 44 24 40
AOB.GNames.DispOffset = 0x3

[Offsets]             ; 数值 RVA 与结构布局
GNames   = 0xCE6CA30
GObjects = 0xCF3F260
```

取值顺序是内置默认值、`[AOB]` 扫描结果、`[Offsets]` 数值键，后面的覆盖前面的。也就是说手动填的值优先级最高。

`SkipAOB = 0` 才会扫签名。安全模式不扫，跟这个开关无关。

仓库里的 RVA 和结构布局是某个特定客户端构建的示例值，不是通用值。游戏更新后这些数字很可能全部失效，别直接信，先用 `SafeMode = 1` 过一遍。

`DMA.MemMap` 接受 `none`、`auto` 或内存映射文件路径，相对路径基于 exe 所在目录解析。`DMA.Pid = 0` 表示按完整镜像名匹配，有多个同名进程时填 PID。`DMA.Remote` 用于 LeechCore 远程 URI。DMA 连不上就直接失败，不会退回到读本机进程。

### 游戏更新后

1. 保持 `SafeMode = 1`，用 IDA 在新 dump 上重新解析 `GObjects` / `GNames`，RVA 填进 `[Offsets]`。签名还能匹配不代表它仍指向同一个全局变量，别只看扫描结果。
2. 想让签名扫描接管就把 `SkipAOB` 设为 `0`，同时把过期的数值 RVA 注释掉，否则手动值会一直盖住扫描结果。
3. 确认后再设 `SafeMode = 0`。

`GNames` 和 `NamePoolBlocksOffset` 是配对使用的：锚点指向池对象本身时用 `+0x20`，指向块表时用 `+0x10`，两者指向同一张表。只改一个会静默读错表，症状是名称全是 `FAIL`。

## 自定义内存后端

实现 `Memory/IBackend.h`，在 `mem.Init` 之前注册，然后在配置里按名字选：

```cpp
struct MyDriverBackend : IBackend {
    const char* GetName() const override { return "mydriver"; }
    bool Attach(const std::string& processName) override { /* ... */ }
    void Shutdown() override { /* ... */ }
    bool ReadRaw(uintptr_t address, void* buffer, size_t size) override { /* ... */ }
    uintptr_t GetModuleBase(const std::string& moduleName) override { /* ... */ }
    size_t GetModuleSize(const std::string& moduleName) override { /* ... */ }
};

Memory::RegisterBackend("mydriver", [] { return (IBackend*)new MyDriverBackend(); });
```

```ini
[General]
Backend = mydriver
```

`Memory/RpmBackend.cpp` 是一个完整带注释的参考实现。页缓存和大块读容错都在 `Memory/Memory.h` 这层做了，后端只需要实现"读成功或者失败"。

## 目录结构

```
Code/Core/      转储与 UE 解析逻辑
Memory/         内存层：IBackend 接口、DmaBackend、RpmBackend、Memory 门面（含 4 KB 页缓存）
Tests/          mock VMM 回归测试
ThirdParty/     MemProcFS 运行时与头文件（需自行下载）
UEDumper/       入口点与 Visual Studio 工程
```

项目不捆绑也不引用任何内核驱动。

## 测试

```powershell
powershell -ExecutionPolicy Bypass -File Tests/Run.ps1
```

在 `x64/dma-tests/` 里用 mock VMM DLL 跑，覆盖 DMA 生命周期、按进程名和 PID 选择、局部读、缓存恢复、稀疏模块拷贝，以及本机 RPM 后端。名称池部分覆盖块遍历、50 万以上的 ID、修改过的 entry 分配长度、UTF-16、游标边界、分配器回绕和读写失败。最后用真实 exe 通过 DMA API 解析 10 个合成对象名。

这些测试不需要硬件，但也不能证明偏移匹配任何真实构建，那一步只能上机验。

## 已知限制

- 转储面对的是活动进程，已释放或半初始化的对象会读出垃圾。Outer / Super / FField 链遍历带步数保护，指针也做了规范性过滤，但 `ObjectsDump.txt` 里偶尔出现垃圾条目是正常的。
- 大块拷贝只把不可读页清零，日志会打印可读百分比。多数字节读不到时整体失败。
- AOB 扫描需要完整的本地模块拷贝，只在全量模式下跑。
- 名称池遍历会校验 `None` 锚点和 entry 边界，布局不对就停下。进程如果一直在变，读取正好撞上正在初始化的 entry 时需要重跑。

## 第三方组件

仓库里不放第三方二进制。DMA 后端用 `LoadLibraryExW` 和 `GetProcAddress` 在运行时加载，`scripts/Fetch-MemProcFS.ps1` 从上游 release 取：

| 组件 | 许可 | 上游 |
|---|---|---|
| MemProcFS（`vmm.dll`、`vmmdll.h`） | AGPL-3.0 | https://github.com/ufrisk/MemProcFS |
| LeechCore（`leechcore.dll`、`leechcore.h`） | GPL-3.0 | https://github.com/ufrisk/LeechCore |
| FTD3XX.dll | FTDI 专有 | https://ftdichip.com/drivers/d3xx-drivers/ |

FTD3XX.dll 不是只有 FPGA 才需要。`leechcore.dll` 直接导入它，缺了这个文件连 `leechcore.dll` 都加载不起来，报 Windows error 126，DMA 后端完全没法初始化。MemProcFS 自带的 `FTD3XXWU.dll` 是 WinUSB 变体，不是 LeechCore 加载的那个。脚本会一并装上；要手工补就从上面的 FTDI 链接取来放进 `ThirdParty/MemProcFS/bin/`。

自己分发二进制时注意：AGPL-3.0 第 5(c) 条要求整个结合作品按 AGPL 授权，所以把上面三个 DLL 和 UEDumper 打包分发的话，那个分发包是 AGPL 结合作品，不是 MIT。把这些文件留在仓库外，UEDumper 自己的代码才能继续用 MIT。

## 许可

UEDumper 本体为 MIT，见 [LICENSE](LICENSE)。第三方组件保留各自许可，见 [ThirdParty/MemProcFS/README.md](ThirdParty/MemProcFS/README.md)。

---
---

# UEDumper (English)

Unreal Engine SDK and offset dumper for Windows x64. It reads the target machine's memory over MemProcFS/LeechCore DMA by default, and can fall back to local ReadProcessMemory.

For authorized security research and interoperability analysis only. You are responsible for confirming you have legal authorization on the target system.

## Features

- Two memory backends: DMA (default, `fpga://algo=0`) and local `rpm`. For your own, implement `IBackend` and register it with `Memory::RegisterBackend`.
- Signatures and offsets both live in `config.ini`, so a game update means editing config, not recompiling.
- The FName pool is walked block by block from the allocator cursor rather than scanned blindly.
- Output goes to `Games/<ProcessName>/DUMP/` as `NamesDump.txt`, `ObjectsDump.txt` and per-package SDK headers.
- Safe mode resolves just the first 10 UObject names, so a wrong offset shows up immediately instead of after a full run.

## Building

Visual Studio 2022 (v143), C++20, x64 only.

The third-party DMA runtime is not in this repository. Fetch it first:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/Fetch-MemProcFS.ps1
```

Then build:

```
msbuild UEDumper.sln -p:Configuration=Release -p:Platform=x64
```

Build through `UEDumper.sln`. Opening the `.vcxproj` directly fails because the `$(SolutionDir)` include paths don't resolve.

Output lands in `x64/Release-DMA/` (`x64/Debug-DMA/` for Debug). The build fails outright if any third-party file is missing, so a successful build always leaves all three DLLs in place. `config.ini` is created on the first build; later builds only refresh `config.example.ini` and leave your config alone.

## Usage

1. Start the game on the target machine.
2. Connect the DMA device to the host.
3. Run `x64/Release-DMA/UEDumper.exe` on the host with the DMA DLLs and `config.ini` beside it, and close anything else using the same device.
4. Leave `SafeMode = 1` on the first run and look at the 10 UObject names it prints. Names like `/Script/CoreUObject` or `Object` mean the offsets are right.
5. Once they look sane, set `SafeMode = 0` for the full run: module copy, AOB, FName, GObjects, SDK generation.

Add `--no-pause` when running from a script, or it will sit at "press any key" on exit.

## Configuration

`config.ini` sits next to the executable:

```ini
[General]
SafeMode = 1          ; 1 = validate the first 10 UObject names only
SkipAOB  = 1          ; 1 = skip the signature scan, use [Offsets] + built-in defaults
Backend  = dma        ; dma | rpm

[DMA]
DMA.Device = fpga://algo=0
DMA.MemMap = none     ; none | auto | map file path
DMA.Debug  = 0        ; 1 = MemProcFS diagnostics
DMA.Pid    = 0        ; 0 = match by full process name

[AOB]                 ; key signatures (update-time fallbacks)
AOB.GNames.Sig        = 48 8D 0D ? ? ? ? 0F 10 00 0F 11 44 24 40
AOB.GNames.DispOffset = 0x3

[Offsets]             ; numeric RVAs and structure layouts
GNames   = 0xCE6CA30
GObjects = 0xCF3F260
```

Values resolve in the order built-in defaults, `[AOB]` scan results, `[Offsets]` numeric keys, each overriding the previous one. Manual values therefore always win.

`SkipAOB = 0` is what actually runs the signature scan. Safe mode never scans, regardless of that switch.

The RVAs and layouts in this repository are example values for one specific client build, not universal. A game update will likely invalidate all of them, so check with `SafeMode = 1` before trusting them.

`DMA.MemMap` takes `none`, `auto`, or a memory map file path; relative paths resolve against the directory containing the executable. `DMA.Pid = 0` matches by full image name, and you set a PID when several matching processes exist. `DMA.Remote` supplies a LeechCore remote URI. If DMA fails to connect it fails outright rather than falling back to reading a local process.

### After a game update

1. Stay in `SafeMode = 1` and re-resolve `GObjects` / `GNames` in IDA on a fresh dump, then put the RVAs in `[Offsets]`. A signature that still matches does not prove it still points at the same global, so don't trust the scan on its own.
2. To hand resolution over to the signature scan, set `SkipAOB = 0` and comment out the numeric RVAs, otherwise the manual values keep overriding it.
3. Set `SafeMode = 0` once the names check out.

`GNames` and `NamePoolBlocksOffset` work as a pair: an anchor pointing at the pool object itself needs `+0x20`, one pointing at the block table needs `+0x10`, and both reach the same table. Changing only one silently reads the wrong table, which shows up as every name resolving to `FAIL`.

## Writing a custom memory backend

Implement `Memory/IBackend.h`, register it before `mem.Init`, then select it by name in the config:

```cpp
struct MyDriverBackend : IBackend {
    const char* GetName() const override { return "mydriver"; }
    bool Attach(const std::string& processName) override { /* ... */ }
    void Shutdown() override { /* ... */ }
    bool ReadRaw(uintptr_t address, void* buffer, size_t size) override { /* ... */ }
    uintptr_t GetModuleBase(const std::string& moduleName) override { /* ... */ }
    size_t GetModuleSize(const std::string& moduleName) override { /* ... */ }
};

Memory::RegisterBackend("mydriver", [] { return (IBackend*)new MyDriverBackend(); });
```

```ini
[General]
Backend = mydriver
```

`Memory/RpmBackend.cpp` is a complete, commented reference implementation. The page cache and large-read tolerance live in `Memory/Memory.h`, so a backend only has to implement "read succeeded or failed".

## Layout

```
Code/Core/      dump and UE parsing logic
Memory/         memory layer: IBackend, DmaBackend, RpmBackend, Memory facade (4 KB page cache)
Tests/          mock VMM regression tests
ThirdParty/     MemProcFS runtime and headers (fetch them yourself)
UEDumper/       entry point and Visual Studio project
```

No kernel driver is bundled or referenced anywhere in this project.

## Tests

```powershell
powershell -ExecutionPolicy Bypass -File Tests/Run.ps1
```

Runs against a mock VMM DLL in `x64/dma-tests/`, covering the DMA lifecycle, process-name and PID selection, partial reads, cache recovery, sparse module copies, and the local RPM backend. The name-pool tests cover block traversal, IDs above 500000, modified entry allocation lengths, UTF-16, cursor boundaries, allocator rollover and failed reads and writes. It finishes by having the real executable resolve 10 synthetic object names through the DMA API.

None of this needs hardware, and none of it proves the offsets match a real build. Only running it on a live target does that.

## Known limitations

- The dump runs against a live process, so freed or half-initialized objects read back as garbage. Outer/Super/FField walks carry step guards and pointers are checked against the canonical range, but the occasional bad entry in `ObjectsDump.txt` is expected.
- Large copies zero-fill only the pages they could not read and print the readable percentage. A majority-unreadable copy fails.
- The AOB scan needs the full local module copy and only runs in full mode.
- Name-pool traversal validates the `None` anchor and entry boundaries and stops on a layout mismatch. A process that keeps changing can require another run if a read lands on an entry mid-initialization.

## Third-party components

No third-party binaries are committed here. The DMA backend loads them at runtime with `LoadLibraryExW` and `GetProcAddress`, and `scripts/Fetch-MemProcFS.ps1` pulls them from the upstream releases:

| Component | License | Upstream |
|---|---|---|
| MemProcFS (`vmm.dll`, `vmmdll.h`) | AGPL-3.0 | https://github.com/ufrisk/MemProcFS |
| LeechCore (`leechcore.dll`, `leechcore.h`) | GPL-3.0 | https://github.com/ufrisk/LeechCore |
| FTD3XX.dll | FTDI proprietary | https://ftdichip.com/drivers/d3xx-drivers/ |

FTD3XX.dll is not an FPGA-only extra. `leechcore.dll` imports it directly, so without that file `leechcore.dll` itself fails to load with Windows error 126 and the DMA backend cannot initialize at all. The `FTD3XXWU.dll` that ships with MemProcFS is a WinUSB variant and not the module LeechCore loads. The fetch script installs it along with the rest; to supply it by hand, take it from the FTDI link above and drop it in `ThirdParty/MemProcFS/bin/`.

One thing to watch if you redistribute a binary: AGPL-3.0 section 5(c) requires the whole combined work to be licensed under the AGPL, so a package bundling those three DLLs together with UEDumper is an AGPL combined work, not an MIT one. Keeping the files out of this repository is what lets UEDumper's own code stay MIT.

## License

UEDumper is MIT, see [LICENSE](LICENSE). Third-party components keep their own terms, see [ThirdParty/MemProcFS/README.md](ThirdParty/MemProcFS/README.md).
