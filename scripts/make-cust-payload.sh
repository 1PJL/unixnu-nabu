#!/bin/bash
# 鐢熸垚 cust 鍒嗗尯闀滃儚锛?4MB FAT锛夛紝鍐呭惈 UniXNU 閰嶇疆 + XNU 鍐呮牳 + ADT
# 鐢ㄦ硶: bash make-cust-payload.sh <kernel.macho> <kernel.macho.adt> <output.img>
set -e
K="${1:?kernel.macho}"
A="${2:?kernel.macho.adt}"
OUT="${3:-cust-payload.img}"
# Resolve before any cd: a relative $OUT must not resolve inside $STAGE later.
OUT_ABS=$(readlink -f "$OUT")

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
# -v/debug=0x14e turn on STARTUP_DEBUG_VERBOSE: kernel_startup_log() then calls
# kprintf once startup_phase passes STARTUP_SUB_KPRINTF, and kprintf before the
# console exists is what stopped the boot at checkpoint 50. Keep the args minimal.
args = "-v serial=3 debug=0x14e -enable_kprintf_spam"
TOML

rm -f "$OUT"
dd if=/dev/zero of="$OUT" bs=1M count=64 status=none
mkfs.vfat -n NABUCUST "$OUT" > /dev/null
( cd "$STAGE" && mcopy -i "$OUT_ABS" -s EFI ::/ )

echo "=== 鍐呭鏍￠獙 ==="
mdir -i "$OUT" -/ ::/EFI/UniXNU/Payloads
ls -l "$OUT"
echo
echo "鍒峰叆: fastboot flash cust $OUT"
rm -rf "$STAGE"
