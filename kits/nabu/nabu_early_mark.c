// 实现：只做普通内存写（base + rowBytes*y + 4*x），不碰任何寄存器，
// 不做内存分配，失败绝不 panic —— 最坏情况就是屏幕上什么也不多。

#include "nabu_early_mark.h"
#include <stddef.h>



volatile uint32_t *g_fb      = NULL;
uint64_t           g_rowBytes = 0;   // 字节/行
uint32_t           g_px       = 0;   // 每行像素数 = rowBytes/4
uint32_t           g_w        = 0;
uint32_t           g_h        = 0;
int                g_slot     = 0;   // 下一个标记画在第几个槽位

static const uint32_t kBlockPx   = 48;  // 每个色块边长（1600x2560 屏幕上足够醒目）
static const uint32_t kGapPx     = 16;
static const uint32_t kMargin    = 32;

static const uint32_t kColors[9] = {
  0,            // 0 未用
  0xFFFFFFFF,   // 1 白
  0xFFFFFF00,   // 2 黄
  0xFF00FFFF,   // 3 青
  0xFF00FF00,   // 4 绿
  0xFFFF00FF,   // 5 品红
  0xFFFF8000,   // 6 橙
  0xFF0000FF,   // 7 蓝
  0xFFFF0000,   // 8 红（参数非法时专用）
};

inline void put_px(uint32_t x, uint32_t y, uint32_t color) {
  g_fb[(uint64_t)y * g_px + x] = color;
}

void fill_rect(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, uint32_t color) {
  if (x0 >= g_w || y0 >= g_h) {
    return;
  }
  uint32_t xe = x0 + w; if (xe > g_w) { xe = g_w; }
  uint32_t ye = y0 + h; if (ye > g_h) { ye = g_h; }
  for (uint32_t y = y0; y < ye; y++) {
    for (uint32_t x = x0; x < xe; x++) {
      put_px(x, y, color);
    }
  }
}



void nabu_mark_init(const uint64_t video[6]) {
  // XNU boot_args.Video 布局：base, rowBytes, width, height, depth, display
  const uint64_t base     = video[0];
  const uint64_t rowBytes = video[1];
  const uint64_t width    = video[2];
  const uint64_t height   = video[3];
  const uint64_t depth    = video[4];

  g_fb       = NULL;
  g_rowBytes = 0;
  g_px       = 0;
  g_w        = 0;
  g_h        = 0;
  g_slot     = 0;

  if ((base == 0) || (rowBytes == 0) || (width == 0) || (height == 0)) {
    return;                     // 没有帧缓冲：静默放弃
  }
  if (depth != 32) {
    return;                     // 只支持 32bpp（本机就是 32）
  }
  if ((width > 8192) || (height > 8192)) {
    return;                     // 参数异常：不画，避免写坏内存
  }

  g_fb       = (volatile uint32_t *)(uintptr_t)base;
  g_rowBytes = rowBytes;
  g_px       = (uint32_t)(rowBytes / 4);
  g_w        = (uint32_t)width;
  g_h        = (uint32_t)height;

  // 先清一条黑底，保证绿条不会干扰判读
  fill_rect(0, 0, g_w, kMargin + kBlockPx + kMargin, 0xFF000000);
}

void nabu_mark(int n) {
  if (g_fb == NULL) {
    return;
  }
  if ((n < 1) || (n > 8) || (g_slot >= 8)) {
    return;
  }
  const uint32_t x = kMargin + (uint32_t)g_slot * (kBlockPx + kGapPx);
  fill_rect(x, kMargin, kBlockPx, kBlockPx, kColors[n]);
  g_slot++;
}
