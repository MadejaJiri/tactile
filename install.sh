#!/bin/bash
# Installs Tactile on this machine and checks that it will still be there
# after a reboot and after the next kernel update.
#
#   ./install.sh
#
# Run it as yourself in a terminal; it asks for your password (through sudo)
# when it installs the package and starts the service.
set -euo pipefail

ROOT="$(dirname "$(readlink -f "$0")")"
KERNEL="$(uname -r)"

say() { printf '\n\033[1m%s\033[0m\n' "$*"; }
ok() { printf '  ok   %s\n' "$*"; }
bad() { printf '  !!   %s\n' "$*"; PROBLEMS=$((PROBLEMS + 1)); }
die() { printf 'install.sh: %s\n' "$*" >&2; exit 1; }
PROBLEMS=0

[ "$(id -u)" != 0 ] || die "run this as yourself, not as root; it asks for sudo when it needs it."
[ "$(uname -m)" = aarch64 ] || die "Tactile is for Apple Silicon MacBooks (this machine is $(uname -m))."
command -v makepkg >/dev/null 2>&1 || die "this needs an Arch-based system (makepkg is missing)."
command -v qmake6 >/dev/null 2>&1 || die "qmake6 is missing: sudo pacman -S --needed base-devel qt6-base qt6-declarative qt6-svg qt6-wayland"
[ -e "/lib/modules/$KERNEL/build/Module.symvers" ] ||
	die "the headers of the running kernel ($KERNEL) are missing. Install them (for example: sudo pacman -S linux-aurora-headers) and run this again. If the kernel was updated since the last boot, reboot first."
[ -d /sys/module/hid_magicmouse ] ||
	die "hid-magicmouse is not loaded, so this does not look like a MacBook with an Apple trackpad under Linux."

say "Building and installing the package"
"$ROOT/bin/install"

say "Starting the service"
sudo systemctl enable --now tactiled.service

say "Checking"

# 1. DKMS built the driver for the running kernel.
installed="$(modinfo -k "$KERNEL" -F filename hid_magicmouse 2>/dev/null || true)"
case "$installed" in
*/updates/*) ok "the driver is built for kernel $KERNEL" ;;
*)
	bad "DKMS did not build the driver for kernel $KERNEL:"
	dkms status tactile 2>&1 | sed 's/^/         /'
	log="$(ls -t /var/lib/dkms/tactile/*/build/make.log 2>/dev/null | head -n 1 || true)"
	[ -z "$log" ] || grep -E "^tactile:|error" "$log" | head -n 10 | sed 's/^/         /'
	;;
esac

# 2. The image the machine boots from carries it. The trackpad's driver is
#    loaded from there, before the disk is unlocked, so this is what decides
#    which one runs after a reboot.
#    Prints "stale" if an image for this kernel still has the kernel's own
#    driver, "carried" if it has Tactile's, and "disk" if no image has the
#    driver at all, in which case it is loaded from the disk later.
#    Some setups boot the initramfs as it is, others a unified kernel image
#    made from it on the EFI partition, which only root may list; both count.
boot_image() {
	local image files state=disk
	while IFS= read -r image; do
		case "$image" in *fallback*) continue ;; esac
		files="$(sudo lsinitcpio "$image" 2>/dev/null </dev/null | grep "/$KERNEL/.*hid-magicmouse" || true)"
		case "$files" in
		"") ;;
		*/updates/*) state=carried ;;
		*)
			echo stale
			return
			;;
		esac
	done < <(sudo sh -c 'ls /boot/initramfs-*.img /boot/EFI/Linux/*.efi /boot/efi/EFI/Linux/*.efi /efi/EFI/Linux/*.efi 2>/dev/null' </dev/null || true)
	echo "$state"
}

# Omarchy's Limine setup wraps mkinitcpio, so that the unified image is made
# again as well.
rebuild_boot_image() {
	if command -v limine-mkinitcpio >/dev/null 2>&1; then
		sudo limine-mkinitcpio >/dev/null 2>&1 || true
	else
		sudo mkinitcpio -P >/dev/null 2>&1 || true
	fi
}
case "$installed" in
*/updates/*)
	if ! command -v lsinitcpio >/dev/null 2>&1; then
		echo "  ..   no mkinitcpio here: if your boot image includes hid-magicmouse, rebuild it"
	else
		state="$(boot_image)"
		if [ "$state" = stale ]; then
			echo "  ..   the boot image still has the kernel's own driver; rebuilding it"
			rebuild_boot_image
			state="$(boot_image)"
		fi
		case "$state" in
		carried) ok "the boot image loads it" ;;
		disk) ok "the boot image leaves the driver to the disk, where Tactile's is the one found" ;;
		*) bad "the boot image still has the kernel's own driver (sudo mkinitcpio -P shows why)" ;;
		esac
	fi
	;;
esac

# 3. The service is up and reaches the driver.
if systemctl is-active --quiet tactiled.service; then
	ok "the service is running and starts at boot"
else
	bad "the service did not start: journalctl -u tactiled -b"
fi

# 4. What is loaded right now. A driver loaded before this installation stays
#    loaded, whichever it is, until a reboot or a switch by hand.
have="$(modinfo -k "$KERNEL" -F version hid_magicmouse 2>/dev/null || true)"
runs="$(cat /sys/module/hid_magicmouse/version 2>/dev/null || true)"
if [ -n "$runs" ] && [ "$runs" = "$have" ]; then
	ok "Tactile's driver $runs is loaded"
elif [ "$PROBLEMS" = 0 ]; then
	echo "  ..   the installed driver is not the one loaded right now; it takes over at the next boot"
	answer=n
	if [ -t 0 ]; then
		read -r -p "       Switch now? Keyboard and trackpad pause for two seconds. [Y/n] " answer || answer=n
		answer="${answer:-y}"
	fi
	case "$answer" in
	y | Y | yes) sudo tactile-driver reload || bad "switching did not work; a reboot will do it" ;;
	*) echo "       Later: sudo tactile-driver reload, or reboot." ;;
	esac
fi

# 5. The actuator, the part that makes the click. Some boots miss it; the
#    click is the firmware's until the controller is probed again.
if [ -d /sys/bus/spi/drivers/spi-hid-apple-of ]; then
	if ls /sys/bus/hid/drivers/hid-apple-mtp-haptic 2>/dev/null | grep -q -E '^[0-9A-F]{4}:'; then
		ok "the trackpad's actuator is there"
	else
		echo "  ..   the trackpad's actuator was not found at this boot, so the firmware plays the click (tactile-driver help)"
	fi
fi

if [ "$PROBLEMS" != 0 ]; then
	say "Installed, with $PROBLEMS thing(s) to look at above."
	exit 1
fi

say "Done"
tactile status | sed -n '1,5s/^/  /p'
echo
echo "  Open Tactile from the launcher (Super + Space), or run: tactile"
