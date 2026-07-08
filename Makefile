# honey-waybar: waybar adapters for honey's status stream.
#
#   make               build the CLI adapter (honey-waybar), text workspaces
#   make cffi          build the CFFI plugins (needs gtk+-3.0): workspaces,
#                      window, gamma
#   make install       install the CLI adapter + example config/style
#   make install-cffi  install the CFFI plugins
#   make clean

CC     ?= cc
# Distro builds pass CFLAGS on the make command line, which overrides any
# assignment here; required flags live in HONEY_CFLAGS so they survive.
CFLAGS ?= -O2
HONEY_CFLAGS = $(CFLAGS) -std=c11 -Wall -Wextra -Wpedantic
PREFIX ?= /usr/local
DATADIR = $(PREFIX)/share
LIBDIR  = $(PREFIX)/lib

PLUGINS    = honey-workspaces.so honey-window.so honey-gamma.so
GTK_CFLAGS = $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS   = $(shell pkg-config --libs gtk+-3.0)

all: honey-waybar

honey-waybar: src/honey-waybar.c src/status_json.h
	$(CC) $(HONEY_CFLAGS) $(LDFLAGS) -o $@ $<

cffi: $(PLUGINS)

%.so: src/%.c src/waybar_cffi_module.h src/status_feed.h src/status_json.h
	$(CC) $(HONEY_CFLAGS) $(LDFLAGS) -fPIC -shared -Isrc $(GTK_CFLAGS) -o $@ $< $(GTK_LIBS)

install: honey-waybar
	install -Dm755 honey-waybar $(DESTDIR)$(PREFIX)/bin/honey-waybar
	install -Dm644 examples/waybar/config_honey.jsonc \
		$(DESTDIR)$(DATADIR)/honey-waybar/examples/config_honey.jsonc
	install -Dm644 examples/waybar/style.css \
		$(DESTDIR)$(DATADIR)/honey-waybar/examples/style.css
	install -Dm644 README.md \
		$(DESTDIR)$(DATADIR)/doc/honey-waybar/README.md

install-cffi: $(PLUGINS)
	for so in $(PLUGINS); do \
		install -Dm755 $$so $(DESTDIR)$(LIBDIR)/honey-waybar/$$so; \
	done

clean:
	rm -f honey-waybar $(PLUGINS)

.PHONY: all cffi install install-cffi clean
