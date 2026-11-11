#!/bin/bash
# Fetches Tactile and installs it. For one line in a terminal:
#
#   curl -fsSL https://raw.githubusercontent.com/MadejaJiri/tactile/main/get.sh | bash
#
# It keeps the source in ~/.local/share/tactile (override with TACTILE_SRC),
# updates it if it is already there, and runs install.sh from it. Running it
# again later updates Tactile.
set -euo pipefail

SRC="${TACTILE_SRC:-${XDG_DATA_HOME:-$HOME/.local/share}/tactile}"
REPO="https://github.com/MadejaJiri/tactile"

command -v git >/dev/null 2>&1 || {
	echo "get.sh: git is missing: sudo pacman -S --needed git" >&2
	exit 1
}

if [ -d "$SRC/.git" ]; then
	echo "Updating $SRC"
	git -C "$SRC" pull --ff-only
else
	echo "Fetching Tactile into $SRC"
	git clone --depth 1 "$REPO" "$SRC"
fi

# The installer asks questions and for your password. When this script comes
# down a pipe, the keyboard has to be handed to it by name.
if [ -t 0 ]; then
	exec "$SRC/install.sh"
elif [ -r /dev/tty ]; then
	exec "$SRC/install.sh" </dev/tty
else
	echo "get.sh: no terminal to ask from. Run this yourself: $SRC/install.sh" >&2
	exit 1
fi
