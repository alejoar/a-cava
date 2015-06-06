PACKAGE ?= p-cava
VERSION ?= $(shell git describe --always --tags --dirty)

CC       = gcc
CFLAGS   = -std=c99 -Wall -Wextra
CPPFLAGS = -DPACKAGE=\"$(PACKAGE)\" -DVERSION=\"$(VERSION)\" \
           -D_POSIX_SOURCE -D _POSIX_C_SOURCE=200809L
LDLIBS   = -lasound -lm -lfftw3 -lpthread $(shell ncursesw5-config --cflags --libs)

INSTALL     = install
INSTALL_BIN = $(INSTALL) -D -m 755

PREFIX ?= /usr/local
BINDIR  = $(DESTDIR)/$(PREFIX)/bin

debug ?= 0

ifeq ($(debug),1)
CPPFLAGS += -DDEBUG
endif

all: p-cava

p-cava: p-cava.c

install: all
	$(INSTALL_BIN) p-cava $(BINDIR)/p-cava

uninstall:
	$(RM) $(BINDIR)/p-cava

clean:
	$(RM) p-cava

.PHONY: all clean install uninstall
