# 前作对比：0xDive/ios-nabu

调查时间 2026-10-21。仓库 <https://github.com/0xDive/ios-nabu>
（2026-10-19 创建，Python，12 个 open PR/issue，最新 #34）。

## 他们走的是完全不同的路

| 维度 | 0xDive/ios-nabu | 本项目（unixnu-nabu） |
|---|---|---|
| 引导层 | **U-Boot v2026.07**（Qualcomm phone U-Boot，pin `ece349ad...`） | **Mu-Silicium EDK2** + UniXNU |
| 当前目标 | 先解决**日志通道**（USB gadget ACM console / `fastboot oem console`） | 直接启动 XNU |
| 实测进度 | 卡在固件 / USB 栈 bring-up，**没有 XNU 启动成果** | **XNU 内核已进入执行**（`DARWINBOOT: hand-off to the kernel`） |

## 他们的具体卡点（PR #30–#34）

目标是让 Qualcomm USB 栈在 EDK2 里活过来，以便用 USB 当串口：

- 拿到 `PciHostBridge` 的 PE32 能 `LoadImage` 成功，但
  `EFI_PCI_ROOT_BRIDGE_IO` / `EFI_PCI_IO` / `EFI_USB2_HC` / `EFI_USB_IO` **全部缺失**
- 依据旧开源 `XhciPciEmulation.c`，认为应绑定 `QCOM_USB_CONFIG_PROTOCOL`
  （GUID `E722B03F-B250-42CE-8EBD-5BD51812D037`，已在其 pinned SM8150 二进制里验证存在），
  `EFI_USB_INIT` GUID `2DA15AC7-4909-4C4C-B8C3-A742195EA46E`
- 调整 DXE 顺序（`pci-first` / `usb-stack-first`）会在 **EFI Shell 之前同步异常**：
  `ELR 0x13FE0C440`、`ESR 0x02000000`（EC=0）、`FAR=0`
  → 离线符号化到 `ArmCpuDxe::UpdateRegionMappingRecursive()`，
  栈里有 `DxeCore::SetUefiImageMemoryAttributes()` / `ProtectUefiImage()`
- 试过 `PcdImageProtectionPolicy = 0` 关掉镜像保护来定位，仍未跑通
- Stage 8–11 大量 "host-only 审计"（U-Boot 编译、nabu DTS、PM8150B Type-C/PHY），
  所有产物一律标 `safe_to_boot = false`，**从未发布可启动镜像**

## 对我们有用的三条情报

### 1. EDK2 镜像保护的坑（记下来）

`ArmCpuDxe::UpdateRegionMappingRecursive` + `PcdImageProtectionPolicy` 组合，
在我们改 fdf / APRIORI / FD 尺寸时**没有踩到**，但一旦改动镜像布局或权限，
可能触发同类早期 MMU 崩溃。排查手段：`PcdImageProtectionPolicy = 0` 做单变量对照。

### 2. nabu 公共 DTS（PMGR 阶段的现成资料）

```
repo:   https://github.com/SeimoDev/linux-xiaomi-nabu
commit: 4e6d08bb1b467c721e19cd693e4ec7388d982d73
file:   arch/arm64/boot/dts/qcom/sm8150-xiaomi-nabu.dts
兼容:   "xiaomi,nabu", "qcom,sm8150"
```

PMGR / UFS / 时钟 / 稳压器节点**直接抄这个源**，比逆向 ApplePMGR 快得多。
注意：需要配套的 `sm8150-wcd9340.dtsi`，U-Boot 自带树里没有。

### 3. USB / PHY 硬件参数（以后写 USB、充电驱动要用）

| 项 | 值 |
|---|---|
| USB1 wrapper | `0x0A6F8800`（OTG，High-Speed） |
| USB1 HS PHY | `0x088E2000` |
| USB2 controller | `0x0A800000`（host，**SuperSpeed Plus**） |
| PHY 供电 | 0.880V(PLL) / 1.800V(digital) / 3.072V(analog) |
| Type-C | `qcom,pm8150b-typec` + `qcom,pm8150b-vbus-reg` |
| Android 实测 | `/sys/class/udc/a600000.dwc3`：`state=configured`，`maximum_speed=high-speed`，`is_otg=1` |

## 结论

**不跟随他们的路线。** 他们为了拿日志绕去 U-Boot，结果卡在固件层；
本项目已经有可用的日志手段（屏幕 `Print` + 必要时 UART 115200），
并且已经越过他们没到过的位置（内核执行）。

可借鉴的只有上面三条**资料性**情报，尤其是 nabu 公共 DTS —— 直接服务于 PMGR。
