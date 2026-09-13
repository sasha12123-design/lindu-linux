PREFIX ?= /usr/local
DESTDIR ?=

CC ?= gcc
CFLAGS ?= -O2 -std=c99 -Wall -Wextra
LDFLAGS ?=
LIBS = -lX11 -lXinerama

SRC = lindu-wm.c
OBJ = $(SRC:.c=.o)

all: lindu-wm

lindu-wm: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)

$(OBJ): config.h

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: lindu-wm
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 lindu-wm $(DESTDIR)$(PREFIX)/bin/lindu-wm

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/lindu-wm

clean:
	rm -f lindu-wm *.o

.PHONY: all install uninstall clean