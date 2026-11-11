#!/bin/sh
# Refuse to build the patched hid-magicmouse for a kernel it was not made for.
#
#   abi-check.sh /lib/modules/<release>/build
#
# The module is a patched copy of one kernel driver and shares two private
# structures with code built into the kernel (hid-haptic). There is no stable
# interface between them, so a copy built against a kernel where either side
# changed could corrupt memory. Three things have to match what the patch was
# made from; if one does not, nothing is built and the kernel's own driver
# stays in use, which is the stock click.
set -eu

KDIR="${1:?usage: abi-check.sh KERNEL_BUILD_DIR}"
HERE="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
MODDIR="$(dirname -- "$KDIR")"

refuse() {
	echo "tactile: $*" >&2
	echo "tactile: not building for this kernel; its own trackpad driver stays in use." >&2
	exit 1
}

# 1. The helper module the click waveforms go through.
[ -r "$KDIR/Module.symvers" ] || refuse "no Module.symvers in $KDIR (are the kernel headers installed?)"
for symbol in apple_taptic_send apple_taptic_switch_modes hid_haptic_init; do
	grep -q -w "$symbol" "$KDIR/Module.symvers" || refuse "this kernel does not export $symbol"
done

# 2. The kernel's own hid-magicmouse still has the click options the patch was
#    written against, so the driver underneath has not been reworked.
#    Once Tactile's driver is installed, DKMS keeps the kernel's own in its
#    archive instead, and puts it back when Tactile is removed.
stock="$(ls "$MODDIR"/kernel/drivers/hid/hid-magicmouse.ko* \
	/var/lib/dkms/tactile/original_module/"$(basename -- "$MODDIR")"/*/hid-magicmouse.ko* \
	2>/dev/null | head -n 1 || true)"
[ -n "$stock" ] || refuse "this kernel has no hid-magicmouse of its own to replace"
modinfo -F parm "$stock" | cut -d: -f1 | sort >"$HERE/.stock.params"
if ! cmp -s "$HERE/.stock.params" "$HERE/abi/stock.params"; then
	rm -f "$HERE/.stock.params"
	refuse "this kernel's hid-magicmouse has different options than the one the patch was made from"
fi
rm -f "$HERE/.stock.params"

# 3. The structures shared with the built-in hid-haptic code are laid out as
#    in the hid-haptic.h shipped here.
command -v pahole >/dev/null 2>&1 || refuse "pahole is needed to check the kernel's structures"
[ -r "$KDIR/vmlinux" ] || refuse "no vmlinux in $KDIR to check the kernel's structures against"
layout="$(pahole -C hid_haptic_device,hid_haptic_effect "$KDIR/vmlinux" 2>/dev/null |
	sed 's#/\*[^*]*\*/##g' | tr -s ' \t\n' ' ' | sed 's/^ //; s/ $//')"
[ "$layout" = "$(cat "$HERE/abi/hid-haptic.layout")" ] ||
	refuse "this kernel lays out its haptic structures differently"

echo "tactile: kernel matches, building."
