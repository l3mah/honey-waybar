# w3ld-waybar — waybar adapters for w3ld's status stream.
#
#   make               build the CLI adapter (w3ld-waybar)  → text workspaces
#   make cffi          build the CFFI plugins (needs gtk+-3.0) → workspaces,
#                      window, gamma
#   make install       install the CLI adapter + example config/style
#   make install-cffi  install the CFFI plugins
#   make clean

CC     ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic
PREFIX ?= /usr/local
DATADIR = $(PREFIX)/share
LIBDIR  = $(PREFIX)/lib

PLUGINS    = w3ld-workspaces.so w3ld-window.so w3ld-gamma.so
GTK_CFLAGS = $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS   = $(shell pkg-config --libs gtk+-3.0)

all: w3ld-waybar

w3ld-waybar: src/w3ld-waybar.c src/status_json.h
	$(CC) $(CFLAGS) -o $@ $<

cffi: $(PLUGINS)

%.so: src/%.c src/waybar_cffi_module.h src/status_feed.h src/status_json.h
	$(CC) $(CFLAGS) -fPIC -shared -Isrc $(GTK_CFLAGS) -o $@ $< $(GTK_LIBS)

install: w3ld-waybar
	install -Dm755 w3ld-waybar $(DESTDIR)$(PREFIX)/bin/w3ld-waybar
	install -Dm644 examples/waybar/config_w3ld.jsonc \
		$(DESTDIR)$(DATADIR)/w3ld-waybar/examples/config_w3ld.jsonc
	install -Dm644 examples/waybar/style.css \
		$(DESTDIR)$(DATADIR)/w3ld-waybar/examples/style.css
	install -Dm644 README.md \
		$(DESTDIR)$(DATADIR)/doc/w3ld-waybar/README.md

install-cffi: $(PLUGINS)
	for so in $(PLUGINS); do \
		install -Dm755 $$so $(DESTDIR)$(LIBDIR)/w3ld-waybar/$$so; \
	done

clean:
	rm -f w3ld-waybar $(PLUGINS)

.PHONY: all cffi install install-cffi clean
