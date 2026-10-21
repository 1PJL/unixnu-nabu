# 参考工程：mytiantian001/iPadOS-nabu

> 来源：https://github.com/mytiantian001/iPadOS-nabu
> 记录时间：2026-10-22（本仓库第 6–7 轮 bring-up 期间）
> 用途：**同类项目对照**。不是本仓库的依赖，是差距清单。

## 1. 它做到了什么（对方 README 自述）

在同一台 **Xiaomi Pad 5 / nabu / SM8150-AC** 上：

| 部分 | 状态 |
|---|---|
| 启动 | 解锁 stock ABL → `fastboot boot` 自家 shim → XNU |
| CPU | 1 核稳定；8 核能到 userspace |
| 存储 | 自家 UFS host driver；iPadOS root 在 `userdata` 的 HFS+，可读写 |
| **用户态** | **真实 iPad7,3 / iPadOS 16.6.1 (20G81) root 与 shared cache；launchd 与系统 daemon 能跑** |
| **SpringBoard** | **SpringBoard、backboardd、AKS 都能起** |
| 显示服务 | `AppleCLCD`、`IOMobileFramebuffer`、`IOSurface` 已注册；1600×2560 面板已亮 |
| 按键 / AKS | PMIC PON / GPIO 正常；AKS selector 正常，keybag 补齐 |
| 日志 | `userdata` 末尾 4 MiB **持久 ring**，可用 TWRP 读取 |

未完成：iPad UI 真正显示（backboardd 只提交过一次 `SwapEnd` 就 `RequestPowerChange(0)` 关屏）、
GPU accelerator、触摸（Novatek NT36523 SPI）。

## 2. 关键事实：**它也是自编 XNU**

- 内核基线：**xnu-8796（iOS 16 世代）**，来自 Apple 开源发行版
- 不是"原版 iPadOS kernel"；用户态是真实的 iPadOS 16.6.1 二进制
- patch 数量：**83 个**（`xnu/patches/0001…0083`）

> ⚠️ 本仓库此前的判断"对方用的是原版 kernel"是**错的**，以本节为准。

## 3. 与本仓库（UniXNU / xnu-10063.141.1）的差异

| 项 | 本仓库 | 对方 |
|---|---|---|
| 内核基线 | **xnu-10063.141.1**（iOS 17 / macOS 14 世代） | **xnu-8796**（iOS 16 世代） |
| 匹配的用户态 | 无（10063 与 iPadOS 16.6.1 的 ABI 不匹配） | iPadOS 16.6.1 (20G81) |
| 当前进度 | 内核跑到 `kernel_bootstrap()` 内部 | SpringBoard / backboardd / AKS |
| 可见性手段 | 自研方块+数字探针（`nabu_mark`） | `nabu_early` 文本 console + 持久 log ring |

**结论**：无论愿意与否，**要跑真实 iPadOS 用户态，内核基线必须是 8796 世代**。
10063 即使内核完全跑通，也无法加载 iPadOS 16.6.1 的用户态。

## 4. 可直接借鉴的三个 patch

### 4.1 `0010-nabu-early-framebuffer-debug.patch` —— 早期 fb 文本 console

核心机制（原文注释）：
```c
/* start.s maps the splash framebuffer VA=PA as Normal Non-Cacheable memory.
 * Stop using that mapping before arm_vm_init tears down bootstrap TTBR0. */
```

- 在 **`start.s`** 就把 splash framebuffer 建成 **VA=PA、Normal Non-Cacheable**
  → 写入立即生效，**不需要 cache flush**
- `nabu_early_putc()` 用 `iso_font[256*16]`（8×16 位图）**直接打印文本**，
  行滚动、`\r`/`\n` 处理，全静态状态，可在 `panic()` 里用
- 在 `arm_vm_init()` 里、`set_mmu_ttb(invalid_ttep & TTBR_BADDR_MASK)` **之前**
  调 `nabu_early_disable()`
- 打点位置：`arm_init` 入口 / `PE_init_platform` 后 / `ml_parse_cpu_topology` 后 /
  `cpu_bootstrap` 后 / `kernel_startup_bootstrap` 后 / `cpu_init` 后 / 进 `arm_vm_init` 前

**对本仓库的意义**：我们现在的方块探针只能传"第几号"，
换成文本能直接打印"卡在哪个函数"，信息量大一个数量级。

### 4.2 `0012-nabu-keep-debug-console-after-ttbr0.patch` —— TTBR0 之后保住 console

说明"换页表之后 console 就没了"是这条路线的**共性坑**，不是我们独有的问题。

### 4.3 `0047-nabu-persistent-console-log.patch` —— 持久 ring log

把 console 输出写进 `userdata` 末尾 4 MiB 的 ring，
**重启后用 TWRP 读出来**——彻底绕开"屏幕上什么都看不到"的困境。

**这是本仓库最该先抄的一件东西**：比任何屏幕探针都可靠。

## 5. 其它值得对照的 patch（按本仓库缺失可能性排序）

| patch | 作用 | 为何相关 |
|---|---|---|
| `0004-nabu-machine-config` | 机器配置 | 平台常量 |
| `0006-nabu-master-config-open-source-asm-pmap` | 开源 asm/pmap 适配 | 我们也有 4K pmap 系列，需对照 |
| `0011-nabu-physmap-alignment-padding` | physmap 对齐补齐 | VM 初始化相关 |
| `0016-nabu-device-tree-platform-expert` | DT → platform expert | `PE_init_platform` 之后的行为 |
| `0017-nabu-device-tree-virtual-timer-irq` | DT 虚拟定时器 IRQ | **`kernel_bootstrap` 内时钟初始化** |
| `0018-nabu-boot-cpu-driver` | 启动 CPU 驱动 | **同上** |
| `0013/0014` kmem rng / hmac prng | 去掉 corecrypto 依赖 | 早期随机数 |
| `0025/0035/0038` AMFI / entitlements / AKS | 用户态签名链 | 到 SpringBoard 必需 |
| `0028/0030/0034/0069/0082` UFS 系列 | 自家 UFS 驱动 | root 挂载 |
| `0046-nabu-iosurface-iomfb` | IOSurface + IOMobileFramebuffer | 显示服务 |
| `0058/0060/0062/0083` HID / keys / touch / NT36523 | 输入 | 触摸 bring-up |

## 6. 行动建议（按性价比）

1. **先抄 `0047` 持久 ring log**：一次投入，之后所有"看不到"的问题都变成"读日志"。
2. **再抄 `0010` 的文本 console**：`start.s` 建 VA=PA Non-Cacheable + `iso_font`。
3. **认真评估换基线**：把内核从 10063 换到 **8796**，直接对齐对方 83 个 patch 的
   上下文，是到用户态的唯一现实路径。继续在 10063 上补，永远拿不到 iPadOS 16.6.1 用户态。
