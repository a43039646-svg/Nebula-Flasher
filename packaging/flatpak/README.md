# Flatpak / Flathub packaging status

This directory documents the current packaging blocker and the implementation work needed before a working sandboxed Flasher package can be built. It intentionally does not include a Flathub submission manifest.

## Why the native helper cannot simply be bundled

The current application starts `pkexec /usr/libexec/nebula-flasher-writer`. That executable is installed by the native system-wide installer under the host filesystem. A Flatpak build cannot assume that path exists on the host, and broad host filesystem access or a fake host-helper path is not a safe substitute.

## Required engineering work

1. Implement a sandbox-compatible writer path that uses the system UDisks2 D-Bus API to request an authorized writable block-device file descriptor. Prefer the current `org.freedesktop.UDisks2.Block.OpenDevice` API with exclusive/synchronous/close-on-exec flags on supported UDisks2 versions, and handle older versions deliberately if they must be supported.
2. Enumerate/validate the target using UDisks2 object properties or other sandbox-compatible data rather than assuming direct privileged access to all `/dev` nodes.
3. Unmount the target's child filesystems through UDisks2, confirm the image fits, and revalidate the device identity before writing.
4. Keep the same explicit destructive confirmation, block-device identity check, root/system-device refusal, progress reporting and post-write verification.
5. Use only the narrow D-Bus/device permissions required by this workflow; do not solve the problem by granting unrestricted host filesystem access.
6. Test on a Linux system with Flatpak, UDisks2, `flatpak-builder`, the AppStream tools and a disposable USB drive.
7. Add the proper metainfo file, desktop entry, original icon and screenshots after the real GitHub repository/release URL is known.

## Flathub submission policy note

As currently documented by Flathub, the manifest must not contain AI-generated or AI-assisted content. The maintainer must author the submission manifest independently. AI-generated/assisted code, docs, packaging and other material must be disclosed in the submission with affected areas and approximate extent. Review the official policy before submitting: https://docs.flathub.org/docs/for-app-authors/requirements

This note is not a Flatpak manifest and does not mean the application is ready for Flathub. No Flatpak build, Flathub review or publication has happened yet.
