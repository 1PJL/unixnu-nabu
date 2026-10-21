# nabu 公共设备树蒸馏（PMGR / 存储 / 时钟 / PMIC）

来源（pinned）：
```
https://github.com/SeimoDev/linux-xiaomi-nabu
commit 4e6d08bb1b467c721e19cd693e4ec7388d982d73
file   arch/arm64/boot/dts/qcom/sm8150-xiaomi-nabu.dts
```
> 板级 dts 是 overlay 风格，`gcc` / `ufs*` / `cpus` / `pmic` / RPMh 节点在
> 同 commit 的 `sm8150.dtsi`、`pm8150{,b,l}.dtsi` 里。

板级标识：`compatible = "xiaomi,nabu", "qcom,sm8150"`，`model = "Xiaomi Pad 5"`，
`chassis-type = "tablet"`。

---

## 1. RPMh / 电源管理（**最重要**）

| 节点 | compatible | 地址 | 说明 |
|---|---|---|---|
| `apps_rsc: rsc@18200000` | `qcom,rpmh-rsc` | **`0x18200000 +0x10000`**<br>**`0x18210000 +0x10000`**<br>**`0x18220000 +0x10000`** | **RPMh RSC 实体，三个 bank** |
| `rpmhpd: power-controller` | `qcom,sm8150-rpmhpd` | 无 reg（`apps_rsc` 子节点） | 电源域 |
| `rpmhcc: clock-controller` | `qcom,sm8150-rpmh-clk` | 无 reg（`apps_rsc` 子节点） | RPMh 时钟 |
| `aop_cmd_db: memory@85f20000` | `qcom,cmd-db` | `0x85f20000 +0x20000` | cmd-db |
| `smem` | `qcom,smem` | `memory-region = <&smem_mem>` → `0x86000000 +0x200000` | SMEM |
| `apss_shared: mailbox@17c00000` | `qcom,sm8150-apss-shared` | `0x17c00000 +0x1000` | **apss_shared mailbox（不是 RPMh）** |
| `spmi_bus: spmi@c440000` | `qcom,spmi-pmic-arb` | `0xc440000 +0x1100`、`0xc600000 +0x2000000`、`0xe600000 +0x100000`、`0xe700000 +0xa0000`、`0xc40a000 +0x26000` | SPMI arbiter |
| `pdc: interrupt-controller@b220000` | `qcom,sm8150-pdc` | `0xb220000 +0x30000` | PDC |
| `osm_l3: interconnect@18321000` | `qcom,sm8150-osm-l3` | `0x18321000 +0x1400` | OSM L3 |

> ⚠️ **修正**：SM8150 没有 `qcom,ipcc` 节点，也没有 legacy `qcom,rpm`。
> 之前把 `0x17c00000` 当 RPMh 是错的——它是 apss_shared mailbox。
> **RPMh 必须访问 `0x18200000` 那一片。**

互连（interconnect）节点一览（做带宽/时钟时可能用到）：

| 节点 | compatible | 地址 |
|---|---|---|
| config_noc | `qcom,sm8150-config-noc` | `0x1500000 +0x7400` |
| system_noc | `qcom,sm8150-system-noc` | `0x1620000 +0x19400` |
| mc_virt | `qcom,sm8150-mc-virt` | `0x163a000 +0x1000` |
| aggre1_noc | `qcom,sm8150-aggre1-noc` | `0x16e0000 +0xd080` |
| aggre2_noc | `qcom,sm8150-aggre2-noc` | `0x1700000 +0x20000` |
| compute_noc | `qcom,sm8150-compute-noc` | `0x1720000 +0x7000` |
| mmss_noc | `qcom,sm8150-mmss-noc` | `0x1740000 +0x1c100` |
| dc_noc | `qcom,sm8150-dc-noc` | `0x9160000 +0x3200` |
| gem_noc | `qcom,sm8150-gem-noc` | `0x9680000 +0x3e200` |
| camnoc_virt | `qcom,sm8150-camnoc-virt` | `0xac00000 +0x1000` |

## 2. GCC（时钟 / 复位 / GDSC）

| 节点 | compatible | reg | cells |
|---|---|---|---|
| `gcc: clock-controller@100000` | `qcom,gcc-sm8150` | `0x100000 +0x1f0000` | `#clock-cells = <1>`, `#reset-cells = <1>`, `#power-domain-cells = <1>` |

## 3. UFS（与 `NativeUfs` 对应）

| 节点 | compatible | reg |
|---|---|---|
| `ufs_mem_hc: ufshc@1d84000` | `qcom,sm8150-ufshc`, `qcom,ufshc`, `jedec,ufs-2.0` | `0x1d84000 +0x2500`（std）、`0x1d90000 +0x8000`（ice） |
| `ufs_mem_phy: phy@1d87000` | `qcom,sm8150-qmp-ufs-phy` | `0x1d87000 +0x1000` |

`status = "okay"`；`lanes-per-direction = <2>`；boot device `soc@0/1d84000.ufshc`。

## 4. UFS 供电（写电源驱动要照这个）

| supply | 稳压器 | PMIC 轨 | 电压 | 电流上限 |
|---|---|---|---|---|
| `vcc-supply` | `vreg_l10a_2p5` | pm8150 ldo10 | 2504–2950 mV | 750 mA |
| `vccq-supply` | `vreg_l9a_1p2` | pm8150 ldo9 | 1200 mV | 700 mA |
| `vccq2-supply` | `vreg_s4a_1p8` | pm8150 S4（固定） | 1800 mV | 750 mA |
| `vdda-phy-supply` | `vreg_l5a_0p8` | pm8150 ldo5 | 880 mV | 90.2 mA |
| `vdda-pll-supply` | `vreg_l3c_1p2` | pm8150l ldo3 | 1200 mV | 19 mA |

PMIC 节点（SPMI USID）：

| PMIC | 节点 | compatible | reg |
|---|---|---|---|
| pm8150 | `pmic@0` | `qcom,pm8150`, `qcom,spmi-pmic` | `0x0` |
| pm8150b | `pmic@2` | `qcom,pm8150b`, `qcom,spmi-pmic` | `0x2` |
| pm8150l | `pmic@4` | `qcom,pm8150l`, `qcom,spmi-pmic` | `0x4` |
| pm8009 | **不存在**（板级注释注明 not present） | — | — |

RPMh 稳压器组（`apps_rsc` 下）：`pm8150-rpmh-regulators`（`pmic-id = "a"`）、
`pm8150l-rpmh-regulators`（`pmic-id = "c"`）。

## 5. CPU（与 ADT 的 `cpu-regs` 一致 ✓）

`cpu@0/100/200/300/400/500/600/700`，`reg = <0x0 0xN00>`，
`compatible = "qcom,kryo485"`，`enable-method = "psci"`，
idle 走 `domain-idle-states`（`/cpus/idle-states`，`entry-method = "psci"`）。
板级额外加 `cpu7_opp21 = 2956800000 Hz`。

> 与我们 ADT 里的 `--cpu-regs "0,0x100,...,0x700"` **完全对上**，无需改动。

---

## 对 PMGR 的直接结论

1. **RPMh 访问目标改为 `0x18200000`**（3 个 bank），不是 `0x17c00000`
2. cmd-db `0x85f20000`、SMEM `0x86000000` 维持原判断
3. 电压/时钟不要自己猜：UFS 五路供电与 PMIC USID 直接从本表取
4. `rpmhpd` / `rpmhcc` 是 `apps_rsc` 的**子节点**，意味着电源域与时钟都要
   通过 RPMh 命令通道下发（和苹果的 MMIO PMGR 完全不同）
