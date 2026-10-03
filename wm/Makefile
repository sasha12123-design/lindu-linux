PREFIX ?= /usr/local
DESTDIR ?=

CC ?= gcc
CFLAGS ?= -O2 -std=c99 -Wall -Wextra
LDFLAGS ?=

# Заголовки Xft подключают <ft2build.h>, а в Arch freetype2 лежит в
# /usr/include/freetype2 — этот путь обязателен, иначе сборка падает.
PKGCONFIG ?= pkg-config
XFT_CFLAGS = -I/usr/include/freetype2 $(shell $(PKGCONFIG) --cflags xft 2>/dev/null)
XFT_LIBS   = $(shell $(PKGCONFIG) --libs xft 2>/dev/null || echo -lXft)

LIBS = -lX11 -lXinerama $(XFT_LIBS)

SRC = lindu-wm.c
OBJ = $(SRC:.c=.o)

all: lindu-wm

lindu-wm: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)

$(OBJ): config.h

%.o: %.c
	$(CC) $(CFLAGS) $(XFT_CFLAGS) -c -o $@ $<

install: lindu-wm
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 lindu-wm $(DESTDIR)$(PREFIX)/bin/lindu-wm

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/lindu-wm

clean:
	rm -f lindu-wm *.o

.PHONY: all install uninstall clean