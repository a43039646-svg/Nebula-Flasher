# Nebula Flasher 1.0.2 release checklist

## Checked for this source archive

- [x] Version string set to 1.0.2 in the application source.
- [x] `--version` and `--help` handled before GTK initialization.
- [x] Writer validation tests run without writing to disks (see release report before publishing).
- [x] Release notes describe completed features and known limits.
- [x] Website page separates available functionality from future packaging.

## Verify on the maintainer's machine before declaring a production-ready release

- [ ] Build the full GTK4 program on Linux Mint with `make -j1`.
- [ ] Run `./nebula-flasher --version` and confirm `1.0.2`.
- [ ] Run `make test`.
- [ ] Test writing a disposable USB drive; verify progress and byte comparison.
- [ ] Boot the written USB on a test system, if practical.
- [ ] Test disconnect/failure handling and a target too small for the image.
- [ ] Re-check that the internal system disk never appears as an eligible USB target.
- [ ] Test settings persistence, language restart, theme and shape options.
- [ ] Test at least one additional distribution before claiming cross-distro verification.
- [x] Prepare NF icon assets in standard hicolor sizes for the desktop launcher.
- [ ] Capture clean screenshots before store submission.

## Important distinction

A successful build or one successful flash test is not a certification for every drive, ISO, boot mode or distribution. Keep the public site precise about the tested scope.

## v1.0.1 icon integration checks

- [ ] Confirm the applications menu shows the NF icon after installation.
- [ ] Confirm the running-window/task switcher uses the same icon where supported by the desktop environment.
- [ ] Run `sudo make uninstall` and confirm the Nebula Flasher icon files and launcher are removed.


## v1.0.2 monochrome icon checks
- [x] NF icon master uses a black background and white NF/USB artwork.
- [x] Standard hicolor icon sizes (16–512 px) are included.
- [ ] After installation, verify the icon appears in the applications menu.
