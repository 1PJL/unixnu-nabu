# UniXNU on Xiaomi Pad 5 (nabu / SM8150) — 完整交接文档

> 状态：**XNU 内核已在真机上进入执行**（DarwinBoot `exiting boot services` → `hand-off to the kernel`）
> 当前工作：给内核装"可见性探针"（把黑箱变白箱）。
> ✅ 注入链路已修好并 **CI 跑通**（UniXNU `0c4e9a1` / unixnu-nabu `b7642cf`，run `39612555624` success，
> 方块标记 MARK1–4 已链进 `kernel.macho`）。
> ✅ 同时查实"内核无输出"的**真因**：DarwinBoot 的 `boot_args.Video` 被改成了旧数组顺序，
> 与内核 `struct Boot_Video` 字段错位（详见 **坑 8 更正**）。
> ⬜ 下一步：**重编固件 + 刷机数方块**（第 7 节第 5 步，需要接设备）。

---

## 0. 仓库与产物位置

| 项 | 路径 |
|---|---|
| 本仓库（文档/脚本/kit） | `C:\Users\Administrator\Documents\unixnu-nabu-gh` → https://github.com/Lux12138/unixnu-nabu |
| UniXNU fork（内核构建脚本） | `C:\Users\Administrator\Documents\UniXNU` → https://github.com/Lux12138/UniXNU |
| 内核/payload/镜像产物 | `C:\Users\Administrator\Desktop\unixnu-nabu-kernel-ufs`（**所有 nabu-xnu64-*.img 都在这里**）|
| WSL（固件构建） | `/root/Mu-Silicium`（EDK2/Mu UEFI）、`/root/UniXNU`（上游）、`/root/xnu`（稀疏 clone 的 xnu 参考）|
| fastboot/adb | `C:\platform-tools` |
| git | `C:\Users\Administrator\.dsh-tools\PortableGit\cmd\git.exe` |
| gh | `C:\Program Files\GitHub CLI\gh.exe` |

> ⚠️ 易错点：`Documents\unixnu-nabu-kernel-ufs` 和 `Desktop\unixnu-nabu-kernel-ufs` 都存在，
> **boot 镜像在 Desktop 那个**，之前多次因路径写错导致 `No such file`。

---

## 1. 已打通的链路（真机逐项验证）

```
fastboot boot → BootShim → Mu UEFI → UniXNU 菜单 → cust 分区 FAT payload
→ DarwinBoot → 读 51MB 内核 → 解析 Mach-O → 读 ADT → ADT 校验
→ 分配窗口 → 映射段 → ExitBootServices → 跳转 XNU 内核 ✅
```

真机日志（实拍）：
```
UNIXNU: runxnu entry vol=A2142798 path=\EFI\UniXNU\Payloads\kernel.macho cfgerr=
DB1 kernel read  Success size=0x3345000      (≈51.2MB，注意是十六进制打印)
DB2 kernel parse Success
DB3 adt read     Success size=0x330C
DB4 adt ok  win=0x130000000 keep=0 rd=ffffffffffffffff
DB6 va=0xFFFFFFF00427C000 pa=0x8007C000 args=0xC4364000
DARWINBOOT: inputs validated; exiting boot services
still-there kernel entry code ran
```

---

## 2. 硬坑与解法（按踩坑顺序）

### 坑 1：Load Error
- 误判过"官改 ABL 禁用 boot"，**真正原因是 USB 线**
- 劣质线导致大镜像传输丢数据 → ABL 校验失败
- ✅ 换原装数据线 + 主板后面板 USB 口

### 坑 2：ramdisk 大小区间
| ramdisk | ABL | UEFI |
|---|---|---|
| 16 MB | ❌ Load Error | — |
| 32 MB | ✅ | ✅ |
| 64 MB | ✅ | ✅ |
| 96 MB | ✅ | ❌ 黑屏 |

→ **可用区间 32–64MB**；`header_version=1`、`page_size=2048`

### 坑 3：每次 boot 前必须重启设备
重启后**第一次 boot 成功率最高**；连续 boot 会 Load Error（download buffer 残留）。

### 坑 4：内核不能塞进固件卷（PEI 解压预算）
| FVMAIN | 结果 |
|---|---|
| 8 MB（原始） | ✅ |
| 62 MB（未压缩内核） | ❌ |
| 15 MB（LZMA 内核） | ❌ |

FD 本体上限：**3MB 可用，32MB/60MB 全部起不来**（`[uefi_fd] size` 保持 `0x300000`）。

### 坑 5：没有任何 FAT 卷（最根本）
固件自带存储栈（`UFSDxe`/`PartitionDxe`/`DiskIoDxe`/`Fat`），但安卓分区全是 ext4/f2fs →
`Config->Volume=NULL` → DarwinBoot 报 `NULL input`。

✅ **payload 刷进 `cust` 分区**（1GB，出厂资源分区，不影响系统启动）：
```
\EFI\UniXNU\unixnu.toml
\EFI\UniXNU\Payloads\kernel.macho        (51MB)
\EFI\UniXNU\Payloads\kernel.macho.adt    (13KB)
```
⚠️ 刷前确认完整：`Sending 'cust' (65536 KB)`；曾被截断成 1.2MB，表现为 `config error: unixnu.toml line 1: expected '='`

### 坑 6：DarwinBoot 不被 dispatch
✅ 并进 `Include/APRIORI.inc` 的**第一个** `APRIORI DXE { }` 块（新开块会 `GenFv ERROR 2000`）

### 坑 7：ADT 的 RAMDisk 占位符必须与实际一致
```c
else if (RamdiskOffset != MAX_UINTN) { Status = EFI_UNSUPPORTED; goto Fail; }
```
✅ 生成 ADT 时**不要** `--data-ramdisk` / `--packed-data-ramdisk`，使 `rd=ffffffffffffffff keep=0`

### 坑 8：`boot_args.Video` 布局（⚠️ 之前的结论有错，见下）
`BootVideo.h` 原写 `[base, 1, rowBytes, width, height, 32]`，当时认为 XNU 期望
`[base, rowBytes, width, height, depth, display]`
→ 内核读到 `rowBytes=1 / depth=2560` → `initialize_screen()` 失败 → **静默切回串口**
✅ 改写过一次（并同步改 DarwinBoot 绘制下标）；DarwinBoot 侧自报 `base=0x50000000 row=6400 1600x2560 depth=32`

> **⚠️ 更正（本次会话核实）**：xnu-10063.141.1 的 `boot_args.Video` **不是数组**，是
> `struct Boot_Video`，定义在 `pexpert/pexpert/arm64/boot.h:31-38`：
> ```c
> struct Boot_Video {
>     unsigned long v_baseAddr;  // [0]
>     unsigned long v_display;   // [1]  ← 第 2 个！不是最后一个
>     unsigned long v_rowBytes;  // [2]
>     unsigned long v_width;     // [3]
>     unsigned long v_height;    // [4]
>     unsigned long v_depth;     // [5]
> };
> ```
> 而 `pexpert/arm/pe_init.c:419-429` 是按**字段名**逐个取的：
> ```c
> PE_state.video.v_baseAddr = boot_args_ptr->Video.v_baseAddr;
> PE_state.video.v_display  = boot_args_ptr->Video.v_display;
> PE_state.video.v_rowBytes = boot_args_ptr->Video.v_rowBytes;
> PE_state.video.v_width    = ...->Video.v_width;
> PE_state.video.v_height   = ...->Video.v_height;
> PE_state.video.v_depth    = (...->Video.v_depth >> 0) & 0xFF;   // 低 8 位才是深度
> PE_state.video.v_rotate   = (v_depth>>8 & 0xFF) + (v_depth>>24 & 0xFF);
> PE_state.video.v_scale    = ((v_depth>>16) & 0xFF) + 1;
> ```
> 也就是说：**如果 DarwinBoot 真按 `[base, rowBytes, width, height, depth, display]` 写**，
> 内核会读到 `v_display=rowBytes(非 0，于是走 graphics)、v_rowBytes=width、
> v_width=height、v_height=depth、v_depth=display位段全 0` ——
> **整条 video 参数全错位**，`vcattach` 拿到的行宽/宽高都是错的，表现就是"没有输出"。
>
> ⇒ 这**就是**"内核在 `PE_create_console` 之前静默死亡"的真因（已核实，非推测）：
> `vcattach` 拿到的 `v_rowBytes=1600`（真实 6400）、`v_width=2560`（真实 1600）、
> `v_height=32`（真实 2560）、`v_depth=0` —— 屏幕参数全错，图形 console 起不来。
>
> **核实证据链**：
> 1. `pexpert/arm/pe_init.c:419-429` 按**字段名**读（不是按下标数组读）
> 2. `pexpert/pexpert/arm64/boot.h:31-38` 结构体顺序 = base, display, rowBytes, width, height, depth
> 3. `drivers/DarwinBoot/DarwinBootPkg/Driver/BootVideo.h` 的 `git diff` 显示：
>    **上游原版本来就是正确的结构体顺序**
>    ```c
>    Video[0] = FrameBufferBase;              Video[1] = 1;      // v_display
>    Video[2] = RowBytes;                     Video[3] = HorizontalResolution;
>    Video[4] = VerticalResolution;           Video[5] = 32;
>    ```
>    而"坑 8 修复"把它改成了 `Video[1]=RowBytes … Video[5]=0` —— **方向改反了**。
> 4. `DarwinBoot.c` 的 `DiagDraw` 被同步改成用 `Video[1]` 当 rowBytes（也是反的），
>    而 `Video[2]*Video[4]`（FB size）、`Video[3]/Video[4]`（宽高）等其它调用点
>    **一直是结构体语义、没被改** —— 自证原始代码是对的。
>
> ✅ **已回滚（WSL `/root/UniXNU/drivers/DarwinBoot`）**：
> - `git checkout -- DarwinBootPkg/Driver/BootVideo.h` → 恢复上游结构体顺序
> - `DiagDraw` 的 5 处下标改回 `Video[2]`=rowBytes / `Video[3]`=width / `Video[4]`=height
> - 其它调用点（`Video[2]*Video[4]`=FB size、`Video[3]`/`Video[4]`=宽高、
>   日志里的 base/width/height/row/depth）**本来就是结构体语义，无需改**
>
> ⇒ **结论：上游 HEAD 的 DarwinBoot 就是对的。不需要 patch、不需要 fork。**
> 要做的只是"别再按数组顺序改它"。重编固件后 `Video` 布局即与内核一致。

### 坑 11：内核早期**不能**写帧缓冲（探针曾经自己把内核打死）
`ExitBootServices` 之后帧缓冲**仍然是可写可见的**（已用 DarwinBoot 双色探针证明，见下），
但那是**在 UEFI 的页表下**。内核 `start.s` 之后用的是内核自己的页表，`0x50000000`
这个**物理地址**在内核页表里没有映射 —— 于是 `arm_init` 里那次写**直接 fault**，
内核就地死亡，屏幕保持原样，看起来和"内核没跑"一模一样。

**症状特征**：有日志、有屏幕、无方块，且加标记前后行为不变（因为标记本身就是死因）。

✅ **修法（UniXNU `8272559`）**：
- `nabu_mark_init()` 只清状态，**不碰 fb**
- `NABU_MARK(n)` 只把命中记进 RAM 位图 `g_hits`（RAM 必然可写）
- `nabu_mark_rebind()` 放在 `PE_create_console()` **之后**——此时 `vcattach` 已把
  fb 映射好、`PE_state.video.v_baseAddr` 是**内核虚拟地址**，
  一次性把此前所有命中回放到屏幕

### 坑 12：屏幕顶部绿条**不是** "kernel entry code ran"
`DarwinBoot.c` 里那句提示是错的。证据：
1. `BootShim.S` 全文没有画绿条的代码；Mu-Silicium / UniXNU 也搜不到
2. 四张实拍里绿条高度与字号**比例恒定**，只是拍摄距离不同
3. A/B 探针实验证实：绿条**下方**画的红块/绿块都能正常显示

⇒ 绿条只是 UEFI 侧的固定横幅，**不能**当内核进度用。判读只看方块。

#### A/B 探针实验（决定性）
在 DarwinBoot 里同一次启动画两块（都在绿条下方 y=800）：
| 探针 | 时机 | 结果 |
|---|---|---|
| 绿块 (700,800) | `ExitBootServices` **之前** | ✅ 出现 |
| 红块 (32,800) | `ExitBootServices` **之后** | ✅ 出现 |

⇒ 交棒后 fb 可写可见，**排除**"帧缓冲失效"这条路线。

### 坑 10：`nabu_mark_init(args->Video)` 编译不过（已修）
```
error: passing 'Boot_Video' (aka 'struct Boot_Video') to parameter of
       incompatible type 'const unsigned long long *'
```
✅ 改为按「`unsigned long` 字流」传：`nabu_mark_init((const unsigned long *)&args->Video);`
标记实现同时支持两种解释（结构体顺序优先，自动回退旧数组顺序），
并在 `depth&0xFF != 32` 或走了回退时**画红块**，把布局问题直接暴露在屏幕上。

### 坑 9：EL 不是问题，别再查
- `ModuleEntryPoint.S` 的 `EL1_OR_EL2` 分支：起始 EL2 则留 EL2，起始 EL1 则留 EL1
- 实测 `EL=4`（EL1）→ **ABL 用 EL1 交付**，**往上跳 EL 不可能**
- DarwinBoot 源码本身要求 EL1，与 UniXNU 设计一致 ✅

---

## 3. 当前唯一卡点（正在做）

内核已跳转执行，但**在 `PE_create_console()`（arm_init.c:616）之前静默死亡**，屏幕无任何内核输出。

### 解法：内核最早入口画"方块检查点"
原理：`boot_args.Video` 已给出帧缓冲，内核在 `arm_init()` 最开头直接画 48×48 色块，**不需要 console、不需要字库**。

| 方块数 | 颜色 | 含义 |
|---|---|---|
| 0 | — | 死在汇编入口/MMU，或帧缓冲未映射 |
| 1 | 白 | 进了 `arm_init` |
| 2 | 黄 | 过了 `PE_init_platform` |
| 3 | 青 | 过了 `PE_create_console` ← **console 通了** |
| 4 | 绿 | 过了第二次 `PE_init_platform` |

方块从 `(32,32)` 起、间隔 64px、上面有黑色横条背景。

### 已实现
- `kits/nabu/nabu_early_mark.{c,h}`（纯 C，`gcc -fsyntax-only` 通过）
- `scripts/inject-early-marks.sh`（**参考用**：注入任意给定 xnu 源树；CI 不再调用它）
- ✅ **权威实现在** `UniXNU/scripts/build-xnu-4k-baseline.sh`（commit `18ca785`）：
  补丁循环之后注入 `.cache/xnu-work`，带 pre-clean 可重入，锚点缩进无关，MARK1–MARK4 齐全

> `.github/workflows/build-kernel.yml` 里原来那步「注入 `refer/xnu`」已删除——那棵树不参与编译，
> 留着只会误导；改为构建后新增 `Verify early checkpoint marks` 步骤，自动打印
> `marks inserted` / `WARN: mark? anchor` / `llvm-nm` 符号检查三份证据。

### 注入踩坑史（重要！）
1. CI 独立步骤 → `refer/xnu` 不存在（**refer 源延迟拉取**，`tree.py` 注释 `fetched when a step needs it`）
2. 放进 Build kernel 步骤 → `ls refer/` 显示 `total 0`
3. 放进构建脚本、但**在 `if [ ! -d refer/xnu/.git ]; then` 块内** → CI 已 fetch 过，块被跳过
4. 移出 if 块 → PowerShell `Set-Content -Encoding UTF8` 写入 **BOM+CRLF** → bash 解析失败（已用 Python 归一成 LF）
5. 注入到 `refer/xnu` → 不生效，**真正构建目录是 `.cache/xnu-work`** ✅ 已改
6. 改到 `.cache/xnu-work` → CI **failure**：注入改了 `arm_init.c`，与后续 `git apply` 官方补丁**冲突**
   → ✅ **已修**：注入块移到补丁循环 `done`（第113行）**之后**，并加了 pre-clean 反向清理
   （`native-4k-user-abi.patch`、`zzz-boot-progress.patch` 改 `osfmk/arm/arm_init.c`；
   `source-machine-layer.patch` 改 `osfmk/conf/files.arm64`）
7. 锚点原来是 `assert`，实测发现**缩进敏感**（`\t` vs 空格）会误判
   → ✅ 改为**缩进无关正则** + 无匹配只 `print WARN` 不中断；`#include` 锚点缺失时回落到最后一个 `#include` 行
8. 内核构建会跑**两遍**脚本（bootstrap pass 出编译记录 → 真正的 pass），同一 `.cache/xnu-work`
   → ✅ pre-clean 让注入可重入（`MKU` 先撤、`MKP` 再注），CI 复用工作树也不会污染补丁上下文

### 验证注入是否生效
- ❌ 别只看 CI 控制台（脚本 stdout 被重定向进产物）
- ✅ 查产物 `kernel.log`：`marks inserted` / `nabu_early_mark`
- ✅ 查 `kernel.macho` 二进制含 `nabu_mark`

---

## 4. 关键源码锚点（xnu-10063.141.1 / commit d8b80295，已验证）

| 文件 | 行 | 内容 |
|---|---|---|
| `osfmk/arm/arm_init.c` | 199 | `void arm_init(boot_args * args);` |
| | 370 | `PE_init_platform(FALSE, args);` ← MARK1 锚点 |
| | 616 | `PE_create_console();` ← MARK2/3 锚点 |
| | 619 | `PE_init_printf(FALSE);` |
| `pexpert/arm/pe_init.c` | 425-436 | 从 `boot_args.Video` 填 `PE_state.video`（通用 arm64）|
| `pexpert/gen/pe_gen.c` | 135 | `PE_init_printf(boolean)`（`TRUE`→vcattach）|
| `osfmk/conf/files.arm64` | 51 | `osfmk/arm/arm_init.c	optional config_pmap_ppl`（新文件照此格式追加）|

构建脚本 `UniXNU/scripts/build-xnu-4k-baseline.sh`（行号按 commit `18ca785`）：

| 行 | 内容 |
|---|---|
| 13 | `if [ ! -d refer/xnu/.git ]; then git clone ...; fi`（refer 源，**不参与编译**）|
| 21 | `if [ ! -d .cache/xnu-work/.git ]; then git clone --shared refer/xnu .cache/xnu-work; fi` |
| 40-66 | **nabu pre-clean（MKU）**：撤掉上一次注入，保证补丁 `apply --check` 上下文干净 |
| 76-81 | **补丁应用循环**（`done` 在第 81 行）|
| 82-169 | **nabu 注入块（MKP）**：写 `nabu_early_mark.{c,h}` + `files.arm64` + `arm_init.c` 四个 MARK |
| 992 | `make -C .cache/xnu-work "${XNU_BUILD_TARGET:-build}" ...`（真正编译）|

> ⚠️ 注入**必须**在补丁循环之后：`native-4k-user-abi.patch` 与 `zzz-boot-progress.patch` 都改
> `osfmk/arm/arm_init.c`，`source-machine-layer.patch` 改 `osfmk/conf/files.arm64`。
> 另外内核构建会跑**两遍**脚本（bootstrap pass + 正式 pass），所以 pre-clean 是必需的，不是可选。

---

## 5. 硬件实测数据

| 项 | 值 |
|---|---|
| DRAM | 12 GiB |
| **RPMh RSC** | **`0x18200000` ×3 bank** |
| IPCC/apss_shared | `0x17c00000`（**只是 mailbox，不是 RPMh**）|
| cmd-db / SMEM | `0x85f20000` / `0x86000000` |
| SPMI / PDC | `0xc440000` / `0xb220000` |
| GCC | `0x100000 +0x1f0000`（`#clock-cells=1`）|
| UFS HC / PHY | `0x1d84000`（ice `0x1d90000`）/ `0x1d87000` |
| 时间基准 | 19200000 Hz |
| cust 分区 | `0x40000000`（1GB）|
| CPU | `reg 0x0..0x700`，psci，kryo485（与 ADT `--cpu-regs` 一致）|
| UFS 供电 | vcc 2.504–2.95V(L10A)、vccq 1.2V(L9A)、vccq2 1.8V(S4A)、vdda-phy 0.88V(L5A)、vdda-pll 1.2V(L3C) |
| PMIC | pm8150=0、pm8150b=2、pm8150l=4（**无 pm8009**）|

---

## 6. 命令速查

```bash
# ADT（务必不带 RAMDisk 占位符）
python3 UniXNU/scripts/make-nabu-adt.py --dram-size 0x300000000 \
  --owned-memory-size 0x130000000 --cpu-regs "0,0x100,0x200,0x300,0x400,0x500,0x600,0x700" \
  --ipad-identity --chip-id 0x207 --unique-chip-id 0x0000000000000001 --output kernel.macho.adt

# cust payload
bash unixnu-nabu-gh/scripts/make-cust-payload.sh kernel.macho kernel.macho.adt cust-payload.img

# 刷 + 启（需 fastboot）
fastboot flash cust cust-payload.img
fastboot boot C:\Users\Administrator\Desktop\unixnu-nabu-kernel-ufs\nabu-xnu64-T.img

# 固件（WSL）
cd /root/Mu-Silicium && python3 build_uefi.py -d nabu -r DEBUG
```

**恢复**：长按电源键 10 秒；全程**只写过 `cust`**，`abl`/`xbl`/`bootloader` 从未刷过。

---

## 7. 下个会话第一件事（按顺序）

> ✅ 1–3 已完成（commit `18ca785` @ UniXNU main）。**从第 4 步接着做。**

| # | 事项 | 状态 |
|---|---|---|
| 1 | 注入块移到补丁循环 `done` 之后 | ✅ 完成 |
| 2 | `assert` 改容错（并升级为缩进无关正则） | ✅ 完成 |
| 3 | `bash -n` 校验 → commit/push UniXNU → 触发 CI | ✅ 完成（`18ca785`）|
| 3.5 | 首轮 CI 修注入冲突 → 暴露编译错误 → 再修 | ✅ 完成（见下）|
| 4 | 下载产物 → 查日志/符号 | ✅ 完成（run `39612555624` success）|
| 4.5 | 查真因：`boot_args.Video` 布局错位 | ✅ 确认（见坑 8 更正）|
| 5 | 重编固件（修 Video）→ 重打 cust payload → 刷 + 启 → 数方块 | 🔄 第 1 轮已刷：方块 **0**（见下），已下沉 MARK1 等第 3 轮内核 |
| 6 | 若方块 0：改"写可写 RAM + DarwinBoot 回读" | ⬜ 条件触发 |

### 第 5 步实测（第 1 轮刷机，固件已修 + 内核含 MARK1–4）
真机屏幕实拍读出的关键行：
```
DB1 kernel read  Success size=0x3345000
DB2 kernel parse Success
DB3 adt read     Success size=0x330C
DB4 adt ok win=0x130000000 keep=0 rd=ffffffffffffffff
DARWINBOOT: video base=0x50000000 display=1 row=6400 width=1600 height=2560 depth=32
DARWINBOOT: inputs validated; exiting boot services
```
**结论（三条）**：
1. ✅ **Video 布局修复在硬件上生效**：`display=1 row=6400 width=1600 height=2560 depth=32`
   —— 正是 `struct Boot_Video` 的正确顺序（旧固件这里会打印 `display=6400`）。
2. ✅ **内核入口汇编跑过了**：屏幕顶部有 BootShim 的**绿色横条**
   （BootShim 提示语：`A green bar across the top means the kernel entry code ran`）。
3. ❌ **方块数 = 0**：UEFI 菜单与日志文字**完好无损**，说明内核一次都没写过帧缓冲。

⇒ 死亡点比原 MARK1 更早。`arm_init` 在 `PE_init_platform` 之前还有
`arm_setup_pre_sign()` → `arm_slide_rebase_and_sign_image()`（JOP rebase+sign）
→ `cpu_data_init()`，原 MARK1 插在这些之后，**盲区就在这段**。

**已采取**：MARK1 下沉到 `arm_init` 的**第一条语句之前**，并把标记扩成 5 级
（见上面 v2 方块表）。第二轮只需再刷一次内核（固件不用动）。

### 第 4 步验证结果（run `39612555624`，success）
```
① 补丁冲突：无 patch failed / does not apply
② 注入记录：
   kernel-bootstrap.log : marks inserted: MARK1=True MARK2=True MARK3=True MARK4=True
   kernel.log           : nabu pre-clean: reverted .cache/xnu-work/osfmk/arm/arm_init.c
                          nabu pre-clean: reverted .cache/xnu-work/osfmk/conf/files.arm64
                          marks inserted: MARK1=True MARK2=True MARK3=True MARK4=True
   ← 两遍脚本、pre-clean 生效、第二遍重新注入正确：设计验证通过
③ 符号（kernel.macho 53,760,000 B）：_nabu_mark ×6、_nabu_mark_init ×2
```

### 第 3.5 步：首轮 CI 结果（记录在案）
- run `39611266428` **failure**，但**不再是补丁冲突**——冲突已修好。
  证据：`zzz-boot-progress.patch` 的 `apply --check` 全过程无 `patch failed`。
- 新错误是编译期：
  `arm_init.c:373: passing 'Boot_Video' to parameter of incompatible type 'const unsigned long long *'`
  → 见**坑 10**：`args->Video` 是 `struct Boot_Video`，不是数组。
- 同时由 `pexpert/arm/pe_init.c:419-429` 反查出**坑 8 的结论有误**（display 在第 2 位），
  已在该节写更正与待核实项。
- 修法：传 `(const unsigned long *)&args->Video`，实现对两种布局自适应，布局可疑时画红块。

### 第 4 步：确认注入真的进了二进制

触发方式（workflow_dispatch，`build-kernel.yml`）：
```bash
gh workflow run build-kernel.yml --repo Lux12138/unixnu-nabu
gh run watch --repo Lux12138/unixnu-nabu
gh run download --repo Lux12138/unixnu-nabu -n kernel-nabu -D /tmp/nabu-kernel
```

三处证据（**新增的 `Verify early checkpoint marks` 步骤已经把这三条打好了**）：

```bash
# ① 增量补丁干净应用（没有 apply 失败 = 注入没和补丁打架）
grep -a 'error: patch failed' /tmp/nabu-kernel/logs/kernel.log || echo clean
# ② 注入记录：必须看到 MARK1..MARK4 全 True
grep -a 'marks inserted' /tmp/nabu-kernel/logs/kernel.log
# 期望: marks inserted: MARK1=True MARK2=True MARK3=True MARK4=True
# 若出现 WARN: mark? anchor missing → 上游源码锚点漂移，按第 4 节行号重新核对
# ③ 符号真的链进内核
xcrun llvm-nm --defined-only /tmp/nabu-kernel/kernel/kernel.macho | grep nabu_mark
# 期望: 两行 _nabu_mark / _nabu_mark_init（T）
```
⚠️ **别只看 CI 控制台**（脚本 stdout 被重定向进 `out/nabu/logs/kernel.log`）；
`kernel-bootstrap.log` 是 bootstrap pass 的日志，两遍都会跑注入，两遍都该出现 `marks inserted`。

### 第 5 步：刷机 + 数方块

**5a. 重编固件（必须先做！否则 Video 仍是错的）**
DarwinBoot 已回滚到上游正确版本，重编即生效：
```bash
wsl -e bash
cd /root/Mu-Silicium && python3 build_uefi.py -d nabu -r DEBUG
cd /root/UniXNU && ./unixnu bootimg        # 打包成 Android boot 镜像（UEFI FD + dtb + ramdisk）
```
产物按既有命名递增（当前最新 `nabu-xnu64-T.img`，下一个建议 `-U`），放到
`C:\Users\Administrator\Desktop\unixnu-nabu-kernel-ufs\`。

**5b. 重打 cust payload（内核侧）**
```bash
# ADT（务必不带 RAMDisk 占位符）
python3 UniXNU/scripts/make-nabu-adt.py --dram-size 0x300000000 \
  --owned-memory-size 0x130000000 --cpu-regs "0,0x100,0x200,0x300,0x400,0x500,0x600,0x700" \
  --ipad-identity --chip-id 0x207 --unique-chip-id 0x0000000000000001 --output kernel.macho.adt
bash unixnu-nabu-gh/scripts/make-cust-payload.sh kernel.macho kernel.macho.adt cust-payload.img
```
`kernel.macho` 用 CI 产物 `C:\Temp\nabu-ok\kernel\kernel.macho`（run `39612555624`）。

**5c. 刷 + 启**（刷前确认 `Sending 'cust' (65536 KB)`，别被截断）
```bash
fastboot flash cust cust-payload.img
fastboot boot C:\Users\Administrator\Desktop\unixnu-nabu-kernel-ufs\nabu-xnu64-U.img
```
**每次 boot 前先长按电源 10 秒重启**（坑 3）。

**判读新增的"红块"信号**：若右上角（第 8 槽）出现红块，说明内核侧认为
`Video` 布局仍然对不上 —— 那意味着固件没重编成功，而不是帧缓冲问题。

方块语义（v3：早期只记 RAM，`PE_create_console` 之后一次性回放；从 `(32,32)` 起、间隔 64px）：

> ⚠️ **绿条不是进度信号**（见坑 12）。只数方块。
> 因为 `PE_create_console` 之前无法写屏，**"0 块"只能说明"死在 console 之前"，
> 无法再细分**——这是当前观测手段的硬限制。

| 方块数 | 颜色 | 含义 |
|---|---|---|
| **0** | — | 死在 `PE_create_console()` 之前（`arm_init` 早期 / JOP 签名 / `arm_vm_init`）|
| 4 | 白黄青绿 | 到了 `PE_create_console()`，且 1/2/3 全部经过（正常情况）|
| 5 | +品红 | 全过，包括第二次 `PE_init_platform` |
| 附加红块 | 红 | 右上角独立红块 = Video 布局/深度可疑（固件没生效，或 fallback 生效）|
| 只有 1~3 块 | — | 不会出现：回放一定会把已命中的全部画出，缺号说明位图/几何有问题 |

### 第 6 步（仅方块 0 时）
`Video` 基址在内核早期页表里没有映射（内核 start.s 沿用 UEFI 页表，但只保证 RAM 区域），
所以 `nabu_mark_init` 的写会 fault。两个方向：
1. **先确认 fb 是否真的不可写**：让 DarwinBoot 在 `ExitBootServices` 之后、`DarwinEnter` 之前
   用自己已验证过的 fb 画一个"交棒块"。屏幕上出现交棒块但**没有** MARK1 ⇒ 内核侧 fb 不可写（确诊）。
2. **改用 RAM 传递**：`arm_init` 早期只写静态变量（bss 必然可写），
   在 `arm_vm_init` 之后用 pmap 映射 fb，再一次性 flush 全部检查点到屏幕。

### 备选（若标记路线受阻）
- B：DarwinBoot 在 EL2 交付（需 EL 切换，改动大）
- C：UART 115200（需拆机，**用户不接受**）
- D：内核早期 console 走 GOP（`vcattach` 在 `PE_create_console` 之后才生效，救不了更早死亡）

---

## 8. 文档索引

| 文件 | 内容 |
|---|---|
| `README.md` | 战报：链路 + 坑 + 复现 |
| `HANDOVER.md` | 本文档 |
| `docs/firmware-patches.md` | 固件改动清单 + 已知无效尝试 |
| `docs/nabu-dts-extract.md` | DTS 蒸馏（RPMh 地址纠错 + UFS/PMIC/时钟）|
| `docs/pmgr-plan.md` | PMGR 计划 + RPMh 最小读通路 |
| `docs/early-console-plan.md` | 早期 console + 方块检查点机制 |
| `docs/prior-art-0xDive-ios-nabu.md` | 同类项目对比 |
| `kits/nabu/NabuPlatformPower.cpp` | PMGR 骨架 |
| `kits/nabu/nabu_early_mark.{c,h}` | 方块检查点（纯 C）|
| `scripts/make-cust-payload.sh` | cust payload 生成 |
| `scripts/inject-early-marks.sh` | 标记注入器 |
