# 固件改动清单（Mu-Silicium for nabu）

工作目录：WSL `/root/Mu-Silicium`（EDK2 / Mu UEFI）

## 1. FD 尺寸保持不变

`Resources/Configs/nabu.toml`

```toml
[uefi_fd]
base = 0x9FC00000
size = 0x300000      # 3MB，不要放大（>32MB 会导致固件起不来）
```

## 2. fdf：不要内嵌 unixnu.toml

`Platforms/Xiaomi/nabuPkg/nabu.fdf` 的 `[FV.FvMain]` 里移除：

```
FILE RAW = 7D9C9E2A-3B4F-4C8E-9A11-2B3C4D5E6F70 {
  SECTION RAW = nabuPkg/unixnu.toml
}
```

原因：FV 内嵌配置这条路 `Config->Volume` 恒为 NULL，DarwinBoot 会报 `NULL input`。
改由 `cust` FAT 分区提供配置。

## 3. fdf：保留 DarwinBoot

`[FV.FvMain]` 中必须有：

```
INF UniXNUPkg/Application/UniXNU/UniXNU.inf
INF UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.inf
INF DarwinBootPkg/Driver/DarwinBoot.inf
```

## 4. APRIORI：强制派发 DarwinBoot（关键！）

`Platforms/Xiaomi/nabuPkg/Include/APRIORI.inc`，并入**第一个** `APRIORI DXE { ... }` 块：

```
APRIORI DXE {
  INF MdeModulePkg/Core/Dxe/DxeMain.inf
  ...
  INF DarwinBootPkg/Driver/DarwinBoot.inf      # ← 新增这一行
}
```

不加这行时 `LocateProtocol(gUniXNULoaderProtocolGuid)` 返回 `Not Found`，
DarwinBoot 的 `Boot()` 完全不执行。

不要单独新开一个 `APRIORI DXE {}` 块（GenFv 会报 `ERROR 2000: Invalid parameter`）。

## 5. 如果将来要加自建 DXE 驱动

- 模块放 `Silicium-ACPI/Platforms/Xiaomi/nabu/Drivers/<Name>/`（`nabu/` 是包根）
- dsc `[LibraryClasses]` 需要补：
  ```
  FileExplorerLib|MdeModulePkg/Library/FileExplorerLib/FileExplorerLib.inf
  UefiDriverEntryPoint|MdePkg/Library/UefiDriverEntryPoint/UefiDriverEntryPoint.inf
  ```
  否则链接报 `undefined symbol: _ModuleEntryPoint`

## 6. 已知无效尝试（别再试）

| 做法 | 结果 |
|---|---|
| 把内核 RAW 塞进 FVMAIN | FVMAIN 62MB，PEI 解压失败 → 回 fastboot |
| 内核 LZMA 压缩后塞 FVMAIN | FVMAIN 15MB，仍超预算 → 回 fastboot |
| blob 写在 FD 内、FV 外（FD 60MB） | FD 太大，BootShim 起不来 |
| 注册 ramdisk 到 `0x11000000` | ABL 实际不放这里 → data abort |
| 由 DarwinBoot 驱动入口做 ramdisk 注册 | 驱动入口不执行（见坑 6） |
