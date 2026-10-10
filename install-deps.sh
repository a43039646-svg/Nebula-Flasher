#!/usr/bin/env bash
set -euo pipefail

if [[ $EUID -eq 0 ]]; then
  echo "Run this script as your normal user, not with sudo." >&2
  exit 1
fi

if command -v apt-get >/dev/null 2>&1; then
  sudo apt-get update
  sudo apt-get install -y build-essential pkg-config libgtk-4-dev util-linux policykit-1
elif command -v dnf >/dev/null 2>&1; then
  sudo dnf install -y gcc make pkgconf-pkg-config gtk4-devel util-linux polkit
elif command -v pacman >/dev/null 2>&1; then
  sudo pacman -S --needed gcc make pkgconf gtk4 util-linux polkit
elif command -v zypper >/dev/null 2>&1; then
  sudo zypper install -y gcc make pkg-config gtk4-devel util-linux polkit
elif command -v apk >/dev/null 2>&1; then
  sudo apk add build-base pkgconf gtk4.0-dev util-linux polkit
else
  echo "Unsupported package manager. Install a C compiler, pkg-config, GTK4 development files, util-linux (lsblk), and polkit/pkexec manually." >&2
  exit 1
fi

echo "Dependencies installed. Next run: make -j1"
