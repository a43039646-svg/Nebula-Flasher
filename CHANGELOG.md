# Changelog

## 1.0.2 — monochrome NF icon

- Replace the previous colored NF artwork with the approved black-and-white NF/USB icon.
- Ship desktop icon sizes from 16 px through 512 px and install them into the hicolor theme.


## 1.0.1 — NF application icon

- Added the silver-and-purple NF / USB icon in standard Linux hicolor sizes (64, 128, 256 and 512 px).
- Installed a named desktop icon and aligned the desktop entry ID with the GTK application ID so desktop menus and launchers can identify the application consistently.
- Updated install/uninstall targets to add and remove the icon-theme files and refresh the hicolor cache when available.

## 1.0.0 — first public release

- Promote the tested GTK4 application from the 0.1.x prototype series to the first 1.0.0 release.
- Add `nebula-flasher --version` and `--help`.
- Keep the settings page, saved preferences, NCC settings import, language selector, refresh interval and theme/shape options.
- Keep the whole-disk/USB checks, destructive-operation confirmation, Polkit writer, target-size validation, critical-mount checks, progress reporting and byte-for-byte read-back verification.
- Add release documentation and an explicit note that Flatpak packaging still needs proper sandbox-compatible UDisks2 integration.
