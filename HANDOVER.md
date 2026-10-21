# UniXNU on Xiaomi Pad 5 (nabu / SM8150) — 完整交接文档

> 状态：**XNU 内核已在真机上进入执行**（DarwinBoot `exiting boot services` → `hand-off to the kernel`）
> 当前工作：给内核装"可见性探针"（把黑箱变白箱），**尚未完成**。

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

### 坑 8：`boot_args.Video` 数组错位（已修）
`BootVideo.h` 原写 `[base, 1, rowBytes, width, height, 32]`，XNU 期望 `[base, rowBytes, width, height, depth, display]`
→ 内核读到 `rowBytes=1 / depth=2560` → `initialize_screen()` 失败 → **静默切回串口**
✅ 已修正（并同步改 DarwinBoot 绘制下标）；实测现值 `base=0x50000000 row=6400 1600x2560 depth=32`

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
- `scripts/inject-early-marks.sh`（幂等，锚点 assert）
- **已内联进** `UniXNU/scripts/build-xnu-4k-baseline.sh`

### 注入踩坑史（重要！）
1. CI 独立步骤 → `refer/xnu` 不存在（**refer 源延迟拉取**，`tree.py` 注释 `fetched when a step needs it`）
2. 放进 Build kernel 步骤 → `ls refer/` 显示 `total 0`
3. 放进构建脚本、但**在 `if [ ! -d refer/xnu/.git ]; then` 块内** → CI 已 fetch 过，块被跳过
4. 移出 if 块 → PowerShell `Set-Content -Encoding UTF8` 写入 **BOM+CRLF** → bash 解析失败（已用 Python 归一成 LF）
5. 注入到 `refer/xnu` → 不生效，**真正构建目录是 `.cache/xnu-work`** ✅ 已改
6. 改到 `.cache/xnu-work` → CI **failure**：注入改了 `arm_init.c`，与后续 `git apply` 官方补丁**冲突**
   → 修法（未做完）：注入块移到**补丁循环 `done`（第113行）之后**，并把 `assert a in s` 改容错

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

构建脚本 `UniXNU/scripts/build-xnu-4k-baseline.sh`：
- 13-16：`if [ ! -d refer/xnu/.git ]; then git clone ...; fi`
- 80-82：`if [ ! -d .cache/xnu-work/.git ]; then git clone --shared refer/xnu .cache/xnu-work; fi`
- 103-113：**补丁应用循环（注入必须在这之后）**
- 935：`make -C .cache/xnu-work ...`（真正编译）

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

1. 把 `build-xnu-4k-baseline.sh` 的注入块**移到补丁循环 `done`（第113行）之后**
2. 块内 python 的 `assert a in s, 'markN anchor'` 改为容错打印（不抛异常；`str.replace` 无匹配是 no-op，天然安全）
3. `bash -n` 校验 → commit/push UniXNU → 触发 CI
4. 下载产物 → 查 `kernel.log` 有 `nabu_early_mark` 编译记录 + `kernel.macho` 有 `nabu_mark`
5. 重打 cust payload → 进 fastboot → 刷 + 启 → **数方块**
6. 若方块 0：帧缓冲在 arm_init 未映射 → 改"写可写 RAM + DarwinBoot 回读"

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
