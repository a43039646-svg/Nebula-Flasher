# Nebula Flasher 1.0.2

**Release type:** icon and desktop-integration update to the first public release  
**Platform:** Linux desktop  
**Interface and writer:** C + GTK4  
**License:** MIT

Nebula Flasher writes a local operating-system image to a removable USB drive and verifies the written bytes. The workflow is deliberately simple: choose an image, choose a USB drive, confirm destructive writing, then wait for writing and verification to finish.

## Included in 1.0.2

- Replace the earlier colored icon with a clean monochrome NF/USB identity: black background and white artwork.
- Install the icon in standard desktop sizes from 16 px through 512 px.

- New silver-and-purple NF / USB application icon integrated into the Linux desktop menu and launcher.
- The installer installs hicolor icons at 64, 128, 256 and 512 pixels, and the uninstall target removes them.

## Features carried forward from 1.0.0

- Select uncompressed `.iso`, `.img`, `.raw`, or `.bin` image files.
- Discover whole disks reported by `lsblk` as USB-connected or removable.
- Warn about data loss and require both a checkbox and a second confirmation dialog.
- Run the privileged writer through PolicyKit instead of running the whole graphical interface as root.
- Reject partitions, non-removable/non-USB targets, and the device containing the running operating system.
- Check that the image is a non-empty regular file, that it fits the target, and that it is not stored on the selected target drive.
- Attempt to unmount partitions belonging to the target before writing, and refuse certain critical system mounts.
- Display write progress, estimated transfer speed and remaining time.
- Read back the written bytes and compare them against the source image.
- Offer a settings page with USB refresh interval (1, 2, or 5 seconds), theme accent, interface shape and language selection.
- Persist settings in `~/.config/nebula-flasher/config.ini` and import NCC UI settings on first launch when available.
- Install a launcher in the Linux desktop application menu.
- Include `--version` and `--help` command-line options.

The language selector contains 100+ choices. English, Russian, Ukrainian, Danish, German, French, Spanish, Italian, Portuguese, Dutch, Polish, Czech, Swedish and Norwegian have curated translations for core UI strings. Other language files can fall back to English for strings that have not been translated; the selector is not a promise that all 100+ languages are fully translated.

## Install on Linux Mint / Ubuntu / Debian

Download and extract this archive, then run from the extracted folder:

```bash
chmod +x install-deps.sh install.sh
./install-deps.sh
make -j1
make test
./install.sh
```

The dependency helper supports APT, DNF, pacman, Zypper and APK. Review the package names and prompts before confirming package installation.

After installation, launch **Nebula Flasher** from the applications menu or run:

```bash
nebula-flasher
nebula-flasher --version
```

The installer asks for administrator permission to install the native executable, privileged writer, PolicyKit rule, desktop entry, NF icons and language files.

## Safety notes

**Writing an image permanently destroys data on the selected drive.** Double-check the device path and capacity. Test with a spare USB drive and keep backups of important data. The writer contains several checks, but no disk-writing tool can eliminate all risk from choosing the wrong target or hardware failure.

Use a normal, uncompressed image. `.xz` and `.zip` archives are not decompressed automatically. The current release is Linux-only; it does not include Windows or macOS builds.

## Packaging status

- **Native Linux source release:** included in this archive.
- **Flatpak:** not yet available. The current writer calls a host-installed PolicyKit helper at `/usr/libexec/nebula-flasher-writer`. A proper sandboxed package needs UDisks2-based block-device access and authorization (or another reviewed host integration); simply placing the existing files in a Flatpak would not produce a working flasher. See `packaging/flatpak/README.md`.
- **Flathub:** no submission has been made. The icon and screenshot assets are not finalized. Flathub's current rules also require the maintainer to author the manifest without AI-generated or AI-assisted content and to disclose relevant AI assistance in other parts of the submission; see `packaging/flatpak/FLATHUB_MAINTAINER_NOTES.md`.

## Tests and verification scope

Run the validation tests without writing to any disks:

```bash
make test
```

These tests check that the writer rejects invalid target selection and invalid arguments. They do not test a full USB flash cycle. The first 1.0.0 release has received an initial real USB write test on Linux Mint, but this is not a substitute for broader hardware, distribution and boot verification. Review the checklist in `RELEASE_CHECKLIST.md` before treating the release as production-ready for recovery-critical use.

## Uninstall

```bash
sudo make uninstall
```

Or remove the files listed by the `uninstall` target in `Makefile`.

## License

MIT. See `LICENSE`.
