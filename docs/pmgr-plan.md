# PMGR 移植计划（nabu / SM8150）

内核已经在 nabu 上进入执行（`DARWINBOOT: hand-off to the kernel`）。
接下来最早会 panic 的地方，基本可以确定是 **平台电源管理（PMGR）**：
XNU 在 `bsd_init` 之前就会通过 `ApplePMGR` 家族访问 AON / PMGR 寄存器。

## 0. 先要的东西：一份 panic 日志

没有日志就无从下手，两种取法（优先第一种）：

1. **屏幕**：现在内核已接管显示，panic 会喷在屏幕上 → 手机录像/连拍抓下来
2. **UART**：串口 115200（SM8150 一般 `ttyMSM0`），配置里有 `serial=3`，
   接 USB-TTL 到主板测试点可拿完整 log

日志里重点找：
- `panic(...)` 的第一行与调用栈
- 任何 `ApplePMGR` / `AppleARMIODevice` / `platform-power` 字样的 kext 名
- 最后成功加载的 kext（用它判断是第几个驱动挂的）

## 1. SM8150 侧的真实形态（和苹果完全不同）

> 详见 `docs/nabu-dts-extract.md`：该表来自公共 nabu DTS，地址已核对。


| 层 | 苹果 | SM8150 |
|---|---|---|
| 寄存器访问 | MMIO + AON mailbox | **IPC(CmdDb) 消息**、RPMh 命令 TCS、SPMI |
| 电源域 | PMGR 一大坨 | RPMh（RSC + TCS）、NPA、PDC |
| 时钟 | PMGR clock gates | GCC（`0x100000`） |

已确认基址：

```
RPMh RSC  0x18200000  (0x10000) ×3 banks   <-- 真正的 RPMh 入口
IPCC/apss_shared 0x17c00000  (0x1000)  <-- 只是 mailbox，不是 RPMh
cmd-db    0x85f20000  (0x20000)
SPMI      0x0c440000
watchdog  0x17c10000
timer     0x17c20000
SMEM      0x86000000
GCC       0x100000    (见 scripts/GccUfs-nabu.h 的偏移表)
```

RPMh DRV 关键寄存器（SM8150）：

```
RSC_DRV_CTL_TCS_DATA_HI   0x38
RSC_DRV_CTL_TCS_DATA_LO   0x40
TCS_AMC_MODE_ENABLE       BIT(16)
```

## 2. ApplePMGR 的接口面（逆向结果）

```
ApplePMGR sizeof = 0x4b8a8
vtable slot 177..340   （共 164 个 slot 需要实现）
slot 320  readAon()
slot 321  writeAon(uint64_t)
```

策略：**不要一次写 164 个**。先让 `readAon/writeAon` 和初始化路径工作，
其余 slot 先给安全的空实现（返回 0 / 不做动作），靠 panic 日志逐个补。
这和 sheng 的做法一致：`sheng-platform-power.patch` 只是在 `bsd_init.c` 里
加了一句 `sheng_pmgr_run()`。

## 2.5 RPMh 最小读通路（下一段代码的目标）

```
cmd-db(0x85f20000) 里按 "active"/"wake"/"sleep" 查到 TCS 基址
        ↓
组 AMC 命令 16 字节：{ msg_id, addr, data, wait | cmd<<28 }
        ↓
写 bank + RSC_DRV_CTL_TCS_DATA_HI(0x38) / LO(0x40)，置 TCS_AMC_MODE_ENABLE(BIT16)
        ↓
等 DRV completion（或 SMEM 里的 ack 通道）
        ↓
回读 data → 这就是 XNU 想要的"电源域状态/电压/时钟"
```

判定成功不是"不崩"，而是**能打印出非 0 的真实电压/时钟值**。

## 3. 实施顺序

1. `kits/nabu/NabuPlatformPower.cpp` 骨架（本次已放）
2. `build-nabu-platform-power.py`：把骨架编译成 kit，注入 UniXNU 驱动列表
3. `nabu-platform-power.patch`：镜像 `sheng-platform-power.patch`，
   在 `bsd_init.c` 里加 `nabu_pmgr_run()`
4. 首次只实现：
   - `init()`：探测 IPCC / cmd-db / SMEM，打印基址与版本
   - `readAon/writeAon`：先做 **cmd-db 查询 + RPMh TCS 写**的最小闭环，
     失败则返回 0 而不是崩
5. 抓 panic → 补下一个 slot → 循环

## 4. 判断"是否真的通了"

标准不是"不 panic"，而是：
- 能打印出从 RPMh 读到的真实电压/时钟值（而不是 0）
- UFS 驱动能在 XNU 里枚举到分区（`NativeUfs` 已在核心里）
- 能走到 `bsd_init` 并开始加载 kext

## 5. 风险

- RPMh TCS 是**共享资源**，写错会让整机掉电/挂死（可长按电源恢复）
- 建议每次实验前确保 `cust` payload 完好，随时可重刷
- 不要动 `abl`/`xbl`/`bootloader`
