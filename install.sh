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
carried() {
	local image found=1
	for image in /boot/initramfs-*.img; do
		case "$image" in *fallback*) continue ;; esac
		sudo test -e "$image" || continue
		if sudo lsinitcpio "$image" 2>/dev/null | grep -q "/$KERNEL/updates/.*hid-magicmouse"; then
			found=0
		elif sudo lsinitcpio "$image" 2>/dev/null | grep -q "/$KERNEL/"; then
			return 1
		fi
	done
	return $found
}
case "$installed" in
*/updates/*)
	if carried; then
		ok "the boot image loads it"
	else
		echo "  ..   the boot image still has the kernel's own driver; rebuilding it"
		sudo mkinitcpio -P >/dev/null 2>&1 || true
		if carried; then
			ok "the boot image loads it"
		else
			bad "the boot image does not carry the driver (sudo mkinitcpio -P shows why)"
		fi
	fi
	;;
esac

# 3. The service is up and reaches the driver.
if systemctl is-active --quiet tactiled.service; then
	ok "the service is running and starts at boot"
else
	bad "the service did not start: journalctl -u tactiled -b"
fi

# 4. What is loaded right now.
if [ -e /sys/module/hid_magicmouse/parameters/haptic_debounce_ms ]; then
	ok "Tactile's driver is loaded"
elif [ "$PROBLEMS" = 0 ]; then
	echo "  ..   the kernel's own driver is still loaded; Tactile's takes over at the next boot"
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

if [ "$PROBLEMS" != 0 ]; then
	say "Installed, with $PROBLEMS thing(s) to look at above."
	exit 1
fi

say "Done"
tactile status | sed -n '1,5s/^/  /p'
echo
echo "  Open Tactile from the launcher (Super + Space), or run: tactile"
