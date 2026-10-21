// nabu_early_mark — 内核最早期「进度标记」，不需要字库、不依赖任何子系统。
//
// 设计动机：内核在 PE_create_console 之前就静默死亡，任何 console 都用不上。
// 但 boot_args.Video 已经给出帧缓冲（base / rowBytes / width / height / depth），
// 所以内核只要往显存里画几块色块，就能把「执行到哪一步」传达到屏幕上。
//
// 用法（在 arm_init.c 等最早期的位置）：
//     nabu_mark_init(boot_args_ptr->Video);
//     NABU_MARK(1);            // arm_init 入口
//     ...
//     NABU_MARK(2);            // PE_init_platform 之后
//
// 读法：屏幕左上角出现 N 个色块 ⇒ 内核走到了第 N 个检查点。
//       一个都没有 ⇒ 连 arm_init 都没进去（死在汇编入口/MMU 建立）。
//       色块变红 ⇒ 参数非法（v_baseAddr==0 或 depth!=32），已安全放弃。

#include <stdint.h>

// 标记编号 → 颜色（高对比，便于拍照辨认）
// 1 白 2 黄 3 青 4 绿 5 品红 6 橙 7 蓝 8 红(仅用于报错)
void nabu_mark_init(const uint64_t *video);
#define NABU_MARK(n) nabu_mark(n)
void nabu_mark(int n);
