# stow - simple X text window
# See LICENSE file for copyright and license details.

include config.mk

all: stow

.c.o:
	$(CC) $(STWCFLAGS) -c $<

stow: stow.o
	$(CC) $^ $(STWLDFLAGS) -o $@

stow.o: arg.h config.h config.mk

clean:
	rm -f stow stow.o

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f stow $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/stow
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < stow.1 > $(DESTDIR)$(MANPREFIX)/man1/stow.1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/stow.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/stow \
		$(DESTDIR)$(MANPREFIX)/man1/stow.1

.PHONY: all clean install uninstall
