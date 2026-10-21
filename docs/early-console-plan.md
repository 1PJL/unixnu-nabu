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
