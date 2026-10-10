CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2 -g
WARNINGS = -Wall -Wextra -Wpedantic -Wformat=2 -Wshadow -Wconversion
GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk4 2>/dev/null)
GTK_LIBS := $(shell $(PKG_CONFIG) --libs gtk4 2>/dev/null)

.PHONY: all clean test install uninstall

ICON_ID = io.nebula.Flasher
ICON_SIZES = 16 24 32 48 64 128 256 512
all: nebula-flasher nebula-flasher-writer

nebula-flasher: src/main.c src/i18n.c src/i18n.h
	@test -n "$(GTK_LIBS)" || (echo "GTK4 development files are missing. Run ./install-deps.sh first."; exit 1)
	$(CC) $(CFLAGS) $(WARNINGS) $(GTK_CFLAGS) -o $@ src/main.c src/i18n.c $(GTK_LIBS)

nebula-flasher-writer: src/nebula-flasher-writer.c
	$(CC) $(CFLAGS) $(WARNINGS) -o $@ $<

test: nebula-flasher-writer
	./tests/test-writer-validation.sh ./nebula-flasher-writer

install: all
	install -Dm755 nebula-flasher /usr/local/bin/nebula-flasher
	install -Dm755 nebula-flasher-writer /usr/libexec/nebula-flasher-writer
	install -Dm644 data/io.nebula.Flasher.policy /usr/share/polkit-1/actions/io.nebula.Flasher.policy
	install -Dm644 data/io.nebula.Flasher.desktop /usr/share/applications/io.nebula.Flasher.desktop
	rm -f /usr/share/applications/nebula-flasher.desktop /usr/local/share/applications/io.nebula.Flasher.desktop
	for size in $(ICON_SIZES); do \
		install -Dm644 data/icons/hicolor/$${size}x$${size}/apps/$(ICON_ID).png /usr/share/icons/hicolor/$${size}x$${size}/apps/$(ICON_ID).png; \
	done
	if command -v gtk-update-icon-cache >/dev/null 2>&1; then gtk-update-icon-cache -f /usr/share/icons/hicolor >/dev/null 2>&1 || true; fi
	install -d /usr/local/share/nebula-flasher/locales
	install -m644 locales/*.lang /usr/local/share/nebula-flasher/locales/
	@echo "Installed. Find Nebula Flasher in your applications menu."

uninstall:
	rm -f /usr/local/bin/nebula-flasher
	rm -f /usr/libexec/nebula-flasher-writer
	rm -f /usr/share/polkit-1/actions/io.nebula.Flasher.policy
	rm -f /usr/share/applications/nebula-flasher.desktop /usr/share/applications/io.nebula.Flasher.desktop
	for size in $(ICON_SIZES); do rm -f /usr/share/icons/hicolor/$${size}x$${size}/apps/$(ICON_ID).png; done
	if command -v gtk-update-icon-cache >/dev/null 2>&1; then gtk-update-icon-cache -f /usr/share/icons/hicolor >/dev/null 2>&1 || true; fi
	rm -rf /usr/local/share/nebula-flasher

clean:
	rm -f nebula-flasher nebula-flasher-writer
