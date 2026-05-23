PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man

all:
	$(MAKE) -C mkudffs
	$(MAKE) -C udfinfo
	$(MAKE) -C udflabel

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 mkudffs/mkudffs $(DESTDIR)$(BINDIR)/mkudffs
	install -m 755 udfinfo/udfinfo $(DESTDIR)$(BINDIR)/udfinfo
	install -m 755 udflabel/udflabel $(DESTDIR)$(BINDIR)/udflabel
	install -d $(DESTDIR)$(MANDIR)/man1
	install -d $(DESTDIR)$(MANDIR)/man8
	install -m 644 doc/udfinfo.1 $(DESTDIR)$(MANDIR)/man1/udfinfo.1
	install -m 644 doc/mkudffs.8 $(DESTDIR)$(MANDIR)/man8/mkudffs.8
	install -m 644 doc/udflabel.8 $(DESTDIR)$(MANDIR)/man8/udflabel.8

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/mkudffs
	rm -f $(DESTDIR)$(BINDIR)/udfinfo
	rm -f $(DESTDIR)$(BINDIR)/udflabel
	rm -f $(DESTDIR)$(MANDIR)/man1/udfinfo.1
	rm -f $(DESTDIR)$(MANDIR)/man8/mkudffs.8
	rm -f $(DESTDIR)$(MANDIR)/man8/udflabel.8

clean:
	$(MAKE) -C mkudffs clean
	$(MAKE) -C udfinfo clean
	$(MAKE) -C udflabel clean

.PHONY: all install uninstall clean
