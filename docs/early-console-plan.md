# 内核早期 console 走帧缓冲（不拆机拿到 panic）

## 问题

内核已进入执行（`DARWINBOOT: hand-off to the kernel` / `still-there kernel entry code ran`），
但**屏幕上没有任何 panic 文字**，控制权随后回到 UniXNU（菜单被重绘）。

原因：XNU 早期 `kprintf` 默认输出到**串口**；不接 UART 就是黑箱。
而 UART 需要拆机飞线，`fastboot`/ADB 在此时都不存在。

## 已有的有利条件

DarwinBoot 已经把显存信息交给内核（boot args `Video[6]`）：

```
video base=0x50000000   1600x2560   row 6400 bytes   32 bpp
```

即 `PE_state.video` 里已有 `v_baseAddr / v_rowBytes / v_width / v_height / v_depth`。
帧缓冲是普通内存映射，EL1 下可写 —— **完全不需要额外硬件**。

## 方案：自带一个极小帧缓冲 console

不依赖苹果的 `video_console`（该子系统主要为 x86 启动屏/白屏服务，ARM 侧未启用），
自己写一个自包含的 putc 后端，风险最低。

```
scripts/nabu-early-fb-console/
├── nabu_fb_console.c    ~150 行
│     - 8x16 点阵字库（ASCII 32..126，可先只做 0x20..0x7E 子集）
│     - nabu_fb_putc(c)：处理 \n / \r / 滚屏
│     - 像素写入：base + rowBytes*y + 4*x ，32bpp 直接写 0x00FFFFFF
│     - 全部基于 PE_state.video，不做任何 MMIO / 寄存器操作
└── inject.py            构建期注入
```

### 注入点

1. `PE_init_kprintf()`（`pexpert/gen/pe_gen.c` 或 arm 的实现）
   → 在初始化之后加：
   ```c
   if (PE_state.video.v_baseAddr != 0) { nabu_fb_init(); }
   ```
2. `vputchar()`（`osfmk/console/printf.c` 或 pexpert 对应处）
   → 串口输出之后追加 `nabu_fb_putc(c);`

### 安全约束

- `v_baseAddr == 0` 或 `v_depth != 32` 或宽高异常 → **完全不启用**（直接 return）
- 只做普通内存写；不读、不写任何 SoC 寄存器
- 不做内存分配（早期无 malloc），静态数组保存光标/颜色
- 失败绝不 panic，最坏情况就是"屏幕上什么都不多"

## 实施顺序

1. 拉一份 xnu 源码（CI 或本地 clone 到 `~/xnu`），确认：
   - `PE_init_kprintf` 的真实路径与函数体
   - `vputchar` 的真实路径与签名
   - `PE_state.video` 字段名与类型（`video_info_t`）
2. 写 `nabu_fb_console.c`，用真实字段名；先在本地 `clang -fsyntax-only` 过一遍
3. `inject.py`：基于锚点字符串插入，插入后**校验锚点唯一且插入成功**，失败直接让构建 fail
4. 推 CI 编内核 → 重打 boot.img（`nabu-xnu64-*.img` 同流程）→ 真机 boot
5. 判定：
   - 屏幕出现内核早期文字 / `panic(...)` → console 通了 ✅
   - 仍是只有 UniXNU 菜单 → 内核挂得比 `PE_init_kprintf` 更早
     → 把 fb 初始化再前移（`arm_init` / `machine_startup` 之前），或直接放到
       `Entry.S` 之后的 C 入口第一行

## 与 PMGR 的关系

这是**前置项**：没有早期输出，PMGR 每一步都是盲改。
console 打通后，PMGR 的调试循环变成：
改 `NabuPlatformPower.cpp` → 编 → boot → 屏幕上直接看到读到的电压/时钟值。

---

## 实测进展（2026-10-21 19:49）

### 已修复：视频数组错位（真凶之一）

`DarwinBootPkg/Driver/BootVideo.h` 原来把 `boot_args.Video[6]` 写成了

```
[0]=base  [1]=1  [2]=rowBytes  [3]=width  [4]=height  [5]=32
```

而 XNU（`pexpert/arm/pe_init.c:425`）期望

```
[0]=base  [1]=rowBytes  [2]=width  [3]=height  [4]=depth  [5]=display
```

→ 内核读到 `rowBytes=1 / width=6400 / height=1600 / depth=2560`，
`initialize_screen()` 判定失败 → **静默切回串口** → 屏幕永远没有内核输出。

已修正为正确布局（并同步修正 DarwinBoot 自己绘制文字时的下标）。
修正后实测：`base=0x50000000 row=6400 width=1600 height=2560 depth=32 display=0` ✅

### 但仍无内核输出 → 内核死在 `PE_create_console` 之前

`osfmk/arm/arm_init.c:616` 的 `PE_create_console()` 是第一个可能出字的点。
视频参数已完全正确却仍无输出，说明**没走到那里**。

## 当前第一嫌疑：起始异常级别（EL）不匹配

| 事实 | 说明 |
|---|---|
| DarwinBoot 探针实测 `EL=4`（= EL1） | 我们现在**在 EL1** 交付内核 |
| DarwinBoot 源码要求 `DarwinCurrentEl() == 4` | 它强制 EL1 |
| 苹果真实链路 | **iBoot 在 EL2 交付，XNU 自己降到 EL1** |

XNU 的 `arm_init` / `arm_start` 早期会访问 EL2 相关系统寄存器
（`HCR_EL2`、`VTCR_EL2`、`VHE` 等）。**若内核以为自己在 EL2 而实际在 EL1，
第一条相关指令就触发异常，且此时任何 console 都还没初始化 → 静默死亡。**
这与观察完全一致：视频参数正确、内核入口代码执行了、却一个字都没有。

### 验证方法（下一步，二选一）

**A. 让内核自己说**（最直接）
在 `osfmk/arm/arm_init.c` 的 `arm_init()` 第一行注入一个自包含的
帧缓冲 putc（只写 `boot_args_ptr->Video` 指向的显存，不依赖任何 console）：

```c
nabu_fb_early_init(boot_args_ptr->Video);
nabu_fb_early_puts("arm_init enter\n");
```
再在 `machine_startup`、`PE_init_platform`、`PE_create_console` 前后各打一个标记，
就能精确看到内核走到第几行。

**B. 改 DarwinBoot 在 EL2 交付**
把 `DarwinCurrentEl() != 4` 的判断改成允许 EL2（或直接去掉），
让内核按苹果真实链路从 EL2 起步。风险：DarwinBoot 本身当前跑在 EL1，
要让它把控制权交给 EL2 需要额外做 EL 切换（`eret` 到 EL2），改动更大。

> 建议先做 A（信息量最大、风险最低），确认死点后再决定是否需要 B。

---

## 方案调整：先用「方块检查点」，不做字库

写 8×16 字库要 ~1500 字节点阵数据，容易画花且不易校验；
而**定位死点根本不需要文字**——只要知道"走到第几个检查点"即可。

### 机制

`kits/nabu/nabu_early_mark.c`：往 `boot_args.Video` 指向的显存里画 **48×48 色块**，
不依赖 console、不做分配、不碰寄存器，参数非法就静默放弃（`depth!=32` / `base==0`）。

### 读法（屏幕左上角一行方块）

| 看到的方块数 | 含义 |
|---|---|
| **0 个** | 连 `arm_init` 都没进去 —— 死在汇编入口 / MMU 建立 |
| **1 个（白）** | 进了 `arm_init` |
| **2 个（黄）** | 过了 `PE_init_platform` |
| **3 个（青）** | 过了 `PE_create_console` |
| **4 个（绿）** | 过了 `machine_startup` |
| **N 个红** | 参数非法（已安全放弃，没写坏内存） |

### 注入点（下一轮，改 XNU 源码）

```c
// osfmk/arm/arm_init.c : arm_init() 第一行
nabu_mark_init(boot_args_ptr->Video);
NABU_MARK(1);

// 之后在每关键步之后各插一个
PE_init_platform(...);   NABU_MARK(2);
PE_create_console();     NABU_MARK(3);
// machine_startup 内
```

### 为什么这个方法更优

- 代码量 ~110 行，无点阵数据，几乎不可能"画错"
- 判读只需数方块，照片/录像都能看清
- 失败绝对安全（只做边界检查后的显存写）
- 定位到具体阶段后，再决定是否需要上字库（那时可能已经不需要了）
