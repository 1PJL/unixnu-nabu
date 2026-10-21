# UniXNU on Xiaomi Pad 5 (nabu / SM8150)

在**小米平板 5（nabu，骁龙860 / SM8150）**上通过 UEFI 启动 **苹果 XNU 内核** 的完整移植记录。

> 状态：**内核已真正进入执行**（`DARWINBOOT: hand-off to the kernel` / 绿色 XNU 画面）。
> 下一步是 PMGR 电源管理框架，才能走到 IPSW userspace。

---

## 一、成果（全部真机验证）

```
fastboot boot → Mu UEFI → UniXNU 菜单 → cust 分区 payload
→ DarwinBoot → 读 51MB 内核 → 解析 Mach-O → ADT 校验
→ 分配窗口 → 映射段 → ExitBootServices → 跳转 XNU → 内核执行 ✅
```

关键日志：

```
UNIXNU: runxnu entry vol=A2142798 path=\EFI\UniXNU\Payloads\kernel.macho cfgerr=[]
DB1 kernel read  Success size=53760000
DB2 kernel parse Success
DB3 adt read     Success size=13188
DB4 adt ok  win=120000000 keep=0 rd=ffffffffffffffff
DB arm64 va=0xfffffff007ac2000 pa=0x80070000 owned=0x120000000 window=0x30000000
video base=0x... width=2560 depth=32 bpp
DARWINBOOT: EL1 installed
DARWINBOOT: inputs validated; exiting boot services
still-there kernel entry code ran
DARWINBOOT: hand-off to the kernel
```

---

## 二、六个硬坑与解法（照这个顺序踩，能省几天）

### 坑 1：`fastboot boot` 报 `Failed to load/authenticate boot image: Load Error`

**不是** 官改 ABL 禁用 boot，**是 USB 线**。
- 劣质充电线会让 96MB 级镜像传输丢数据 → ABL 校验失败
- 换原装数据线 + 插主板后面板 USB 口后，同一镜像立刻 `Booting OKAY`

### 坑 2：ramdisk 大小有上下限

| ramdisk | ABL | UEFI |
|---|---|---|
| 16 MB | ❌ Load Error | — |
| 32 MB | ✅ | ✅ 菜单正常 |
| 64 MB | ✅ | ✅ 菜单正常 |
| 96 MB | ✅ | ❌ 黑屏 |

→ **可用区间 32–64MB**；`header_version = 1`，`page_size 2048`。

### 坑 3：设置 `fastboot boot` 前先重启设备

设备每次重启后**第一次 boot 成功率最高**；连续 boot 会出现 Load Error（download buffer 状态残留）。

### 坑 4：把内核塞进固件卷走不通（PEI 解压预算）

| FVMAIN 大小 | 结果 |
|---|---|
| 8 MB（原始） | ✅ 固件正常 |
| 62 MB（未压缩内核） | ❌ 回 fastboot |
| 15 MB（LZMA 压缩内核） | ❌ 回 fastboot |

→ **FVMAIN 上限约 8–10MB**；FD 本体也有上限（3MB 可用，32/60MB 全废）。
结论：**内核不能走 boot.img 内核段，只能走存储**。

### 坑 5：没有任何 FAT 卷（最根本的一个）

固件其实自带完整存储栈（`UFSDxe` / `PartitionDxe` / `DiskIoDxe` / `Fat`），
但安卓分区全是 ext4/f2fs → UniXNU 枚举不到任何带 `unixnu.toml` 的卷
→ `Config->Volume = NULL` → DarwinBoot 报 `NULL input`。

**解法：把 payload 写进一个真实 FAT 分区**

```bash
# cust 分区 1GB（0x40000000），小米出厂资源分区，格式化不影响系统启动
fastboot getvar partition-size:cust
fastboot flash cust cust-payload.img      # 64MB FAT，见 scripts/make-cust-payload.sh
```

FAT 内容：

```
\EFI\UniXNU\unixnu.toml
\EFI\UniXNU\Payloads\kernel.macho        (51MB，含 NativeUfs)
\EFI\UniXNU\Payloads\kernel.macho.adt    (13KB)
```

> ⚠️ `fastboot flash` 前务必确认镜像大小完整（`Sending 'cust' (65536 KB)`）；
> 拷贝被截断会表现为 `config error: unixnu.toml line 1: expected '='`。

### 坑 6：DarwinBoot 驱动不被 dispatch

`LocateProtocol(gUniXNULoaderProtocolGuid)` 返回 `Not Found`，`Boot()` 一行都不执行
（所有 DarwinBoot 补丁"加了没反应"的元凶）。

- fdf 里有 `INF`、FV 里有 FFS、`.inf` 无 Depex 残留、全量 `ConnectController` 都无效
- **解法：并入 `Platforms/Xiaomi/nabuPkg/Include/APRIORI.inc` 的第一个 `APRIORI DXE` 块**

```
APRIORI DXE {
  ...
  INF DarwinBootPkg/Driver/DarwinBoot.inf     # ← 放这里
}
```

### 坑 7（收尾）：ADT 带 RAMDisk 占位符但没提供 initrd

DarwinBoot 源码：

```c
if (Request->Ramdisk != NULL) { ... }
else if (RamdiskOffset != MAX_UINTN) {
    Status = EFI_UNSUPPORTED;   // ← 之前就是卡在这
    goto Fail;
}
```

`DB4 adt ok ... rd=2400` 说明 ADT 里有 RAMDisk 节点，但配置没有 `initrd`。
**解法：生成 ADT 时不要 `--data-ramdisk` / `--packed-data-ramdisk`**，这样
`rd=ffffffffffffffff`，检查通过。

---

## 三、复现步骤

### 1. 编译内核（GitHub Actions `macos-14`）

仓库 `Lux12138/UniXNU` 的 workflows 会产出 `kernel.macho`（51.27MB，含 `NativeUfs`）。
关键点：prelink 阶段需要 `build/storage-abi/IOBlockStorageDevice-reference.o` 与
VerifiedRoot stub，否则 `IOBlockStorageDevice::*` / `_vr_open` 一堆 undefined。

### 2. 生成 ADT（**不要** RAMDisk 占位符）

```bash
python3 UniXNU/scripts/make-nabu-adt.py \
  --dram-size 0x300000000 \
  --owned-memory-size 0x130000000 \
  --cpu-regs "0,0x100,0x200,0x300,0x400,0x500,0x600,0x700" \
  --ipad-identity \
  --chip-id 0x207 \
  --unique-chip-id 0x0000000000000001 \
  --output kernel.macho.adt
```

### 3. 做 cust payload 镜像并刷入

```bash
bash scripts/make-cust-payload.sh     # 产出 cust-payload.img (64MB FAT)
fastboot flash cust cust-payload.img
```

### 4. 组装 UEFI（Mu-Silicium + UniXNU）

```bash
python3 build_uefi.py -d nabu -r DEBUG
```

需要以下固件改动（见 `docs/firmware-patches.md`）：
- `[uefi_fd] size = 0x300000`（保持 3MB）
- fdf 里**移除**内嵌的 `unixnu.toml`（`FILE RAW = 7D9C9E2A-...`），改从 cust 分区读
- fdf 里**保留** `INF DarwinBootPkg/Driver/DarwinBoot.inf`
- `Include/APRIORI.inc` 第一段 `APRIORI DXE` 块加入 DarwinBoot

### 5. 启动

```bash
fastboot boot nabu-xnu.img     # boot.img = BootShim + FD(3MB) + 64MB ramdisk
```

---

## 四、硬件数据（实测，勿用厂商默认值）

| 项目 | 值 | 来源 |
|---|---|---|
| DRAM | 12 GiB（`MemTotal 11766512 kB`） | TWRP |
| userdata 起始 | `21234176` × 512B = `0x288C40` | `/sys/block/sda/sda31/start` |
| userdata 结束 | `0x77BF3FA`，LBA 4096 | 同上 |
| GIC_DIST / REDIST | `0x17a00000` / `0x17a60000` | SM8150 |
| UFS HC | `0x1d84000` | SM8150 |
| 时间基准 | 19200000 Hz | 实测 |
| IPCC / cmd-db / SPMI | `0x17c00000` / `0x85f20000` / `0x0c440000` | SM8150 |
| cust 分区 | `0x40000000`（1GB） | `fastboot getvar` |

---

## 五、下一步（PMGR）

内核已进入执行，会在极早期 panic —— 这是**预期**的：XNU 需要苹果的电源管理框架。

1. **PMGR**：`NabuPlatformPower.cpp`（164 个 vtable slot），SM8150 侧是
   IPCC / cmd-db / SPMI / RPMh（消息式，不是 MMIO）
2. 反查 `ApplePMGR` 的 `readAon()`(slot 320) / `writeAon(uint64_t)`(slot 321) 语义
3. 其余驱动：`NativeRamBlock`、`NativeVirtioBlock`、`NativeHidInput`、
   `NativeLegacyDisplay`、`NativeAdreno`
4. 先抓 panic 日志（屏幕上会显示；或接 UART 115200 拿完整 log），那就是 PMGR 的路线图

---

## 六、工具与环境

- WSL2 Ubuntu 24.04：`/root/Mu-Silicium`（EDK2/Mu UEFI）、`/root/UniXNU`（上游）
- `nabu` 包根：`Silicium-ACPI/Platforms/Xiaomi/nabu/`（**不是** `nabuPkg`，后者只有空 dec）
- Windows：`C:\platform-tools`（fastboot/adb）、`C:\Users\Administrator\.dsh-tools\PortableGit`
- 构建产物：`C:\Users\Administrator\Desktop\unixnu-nabu-kernel-ufs\`

## 七、安全提醒

- **全程只用 `fastboot boot`，不刷 `abl` / `xbl` / `bootloader`**（唯一写操作是把 payload 写进 `cust`）
- `cust` 是出厂资源分区，格式化只丢出厂资源，系统可正常启动
- 任何时刻长按电源键 10 秒可强制重启恢复
