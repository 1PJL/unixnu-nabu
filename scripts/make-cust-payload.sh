#!/bin/bash
# 生成 cust 分区镜像（64MB FAT），内含 UniXNU 配置 + XNU 内核 + ADT
# 用法: bash make-cust-payload.sh <kernel.macho> <kernel.macho.adt> <output.img>
set -e
K="${1:?kernel.macho}"
A="${2:?kernel.macho.adt}"
OUT="${3:-cust-payload.img}"

STAGE=$(mktemp -d)
mkdir -p "$STAGE/EFI/UniXNU/Payloads"
cp "$K" "$STAGE/EFI/UniXNU/Payloads/kernel.macho"
cp "$A" "$STAGE/EFI/UniXNU/Payloads/kernel.macho.adt"

cat > "$STAGE/EFI/UniXNU/unixnu.toml" <<'TOML'
version = 1

[boot]
timeout = 5
default = "xnu"

[kits]
autoload = false

[ui]
title = "UniXNU nabu"
logo = true
console = "auto"

[[entry]]
id = "xnu"
title = "XNU (SM8150 UFS)"
type = "xnu"
path = "Payloads/kernel.macho"
args = "-v serial=3 debug=0x14e"
TOML

rm -f "$OUT"
dd if=/dev/zero of="$OUT" bs=1M count=64 status=none
mkfs.vfat -n NABUCUST "$OUT" > /dev/null
( cd "$STAGE" && mcopy -i "$(readlink -f "$OUT")" -s EFI ::/ )

echo "=== 内容校验 ==="
mdir -i "$OUT" -/ ::/EFI/UniXNU/Payloads
ls -l "$OUT"
echo
echo "刷入: fastboot flash cust $OUT"
rm -rf "$STAGE"
