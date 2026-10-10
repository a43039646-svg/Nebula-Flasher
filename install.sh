#!/usr/bin/env bash
set -euo pipefail
if [[ $EUID -eq 0 ]]; then
  echo "Run install.sh as your normal user; it will ask for sudo when needed." >&2
  exit 1
fi
cd "$(dirname "$0")"
make -j1
sudo make install
