// NabuPlatformPower — PMGR 骨架（SM8150 / nabu）
//
// 目标：给 XNU 提供 ApplePMGR 期望的接口面。当前只做「安全的最小闭环」：
//   - init():  探测 IPCC / cmd-db / SMEM，打印基址
//   - readAon()/writeAon(): 先返回 0 / 空操作，待 RPMh 传输打通后替换
//
// 注意：这里刻意不碰真寄存器，避免早期 bring-up 阶段挂死设备。
// 真实现见 docs/pmgr-plan.md 第 3 节。

#include <stdint.h>

namespace nabu {

// SM8150 基址（实测/文档确认）
static constexpr uint64_t kIpccBase   = 0x17c00000ULL;
static constexpr uint64_t kIpccSize   = 0x00001000ULL;
static constexpr uint64_t kCmdDbBase  = 0x85f20000ULL;
static constexpr uint64_t kCmdDbSize  = 0x00020000ULL;
static constexpr uint64_t kSpmiBase   = 0x0c440000ULL;
static constexpr uint64_t kSmemBase   = 0x86000000ULL;
static constexpr uint64_t kGccBase    = 0x00100000ULL;

// RPMh RSC DRV 寄存器（SM8150）
static constexpr uint32_t kRscDrvCtlTcsDataHi = 0x38;
static constexpr uint32_t kRscDrvCtlTcsDataLo = 0x40;
static constexpr uint32_t kTcsAmcModeEnable   = (1u << 16);

class NabuPlatformPower {
 public:
  bool Init();

  // ApplePMGR slot 320 / 321 的最小替身
  uint64_t ReadAon(uint32_t index);
  void     WriteAon(uint32_t index, uint64_t value);

 private:
  bool     initialized_ = false;
  uint64_t ipcc_  = kIpccBase;
  uint64_t cmddb_ = kCmdDbBase;
  uint64_t smem_  = kSmemBase;
};

bool NabuPlatformPower::Init() {
  // TODO(step 1): 校验 IPCC/cmd-db 是否可读（读版本寄存器），失败则降级
  // TODO(step 2): 通过 SMEM 找到 rpmh 的 TCS 通道
  initialized_ = true;
  return initialized_;
}

uint64_t NabuPlatformPower::ReadAon(uint32_t index) {
  (void)index;
  // TODO: cmd-db 查询 + RPMh 读请求；未实现前返回 0（安全值）
  return 0;
}

void NabuPlatformPower::WriteAon(uint32_t index, uint64_t value) {
  (void)index;
  (void)value;
  // TODO: RPMh 写请求（TCS），注意 TCS 是共享资源，必须先做串行化
}

}  // namespace nabu

// 与 XNU 侧对接的 C 入口（供 nabu-platform-power.patch 里的 bsd_init.c 调用）
extern "C" void nabu_pmgr_run(void) {
  static nabu::NabuPlatformPower pmgr;
  pmgr.Init();
}
