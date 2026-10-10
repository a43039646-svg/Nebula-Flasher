#!/usr/bin/env bash
set -euo pipefail

if [[ ${EUID:-$(id -u)} -eq 0 ]]; then
  echo "Run this script as your normal user, not with sudo." >&2
  exit 1
fi

if command -v apt-get >/dev/null 2>&1; then
  sudo apt-get update
  sudo apt-get install -y build-essential pkg-config libgtk-4-dev util-linux pkexec policykit-1
elif command -v dnf >/dev/null 2>&1; then
  sudo dnf install -y gcc make pkgconf-pkg-config gtk4-devel util-linux polkit
elif command -v pacman >/dev/null 2>&1; then
  sudo pacman -S --needed gcc make pkgconf gtk4 util-linux polkit
elif command -v zypper >/dev/null 2>&1; then
  sudo zypper --non-interactive install -y gcc make pkg-config gtk4-devel util-linux polkit pkexec
elif command -v apk >/dev/null 2>&1; then
  sudo apk add --no-cache build-base linux-headers pkgconf gtk4.0-dev util-linux polkit
  echo "NOTE: On Alpine, ensure the appropriate polkit service backend and a desktop authentication agent are enabled for your init system."
else
  echo "Unsupported package manager. Install a C compiler, make, pkg-config, GTK4 development files, util-linux (lsblk/findmnt), and a working PolicyKit/pkexec setup manually." >&2
  exit 1
fi

missing=0
for tool in cc make pkg-config lsblk findmnt pkexec; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    printf 'WARNING: required command not found after dependency installation: %s\n' "$tool" >&2
    missing=1
  fi
done
if ! pkg-config --exists gtk4 2>/dev/null; then
  echo "WARNING: pkg-config cannot find GTK4 development metadata (gtk4.pc). Check your distribution's GTK4 development package." >&2
  missing=1
fi
if [[ $missing -ne 0 ]]; then
  echo "Some dependencies are still missing. Review the messages above before building." >&2
  exit 1
fi

echo "Dependencies installed and required tools detected. Next run: make clean && make -j1 && make test"
