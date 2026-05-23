PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

TOOLS = mkudffs udfinfo udflabel

all:
	$(MAKE) -C mkudffs
	$(MAKE) -C udfinfo
	$(MAKE) -C udflabel

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 mkudffs/mkudffs $(DESTDIR)$(BINDIR)/mkudffs
	install -m 755 udfinfo/udfinfo $(DESTDIR)$(BINDIR)/udfinfo
	install -m 755 udflabel/udflabel $(DESTDIR)$(BINDIR)/udflabel

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/mkudffs
	rm -f $(DESTDIR)$(BINDIR)/udfinfo
	rm -f $(DESTDIR)$(BINDIR)/udflabel

clean:
	$(MAKE) -C mkudffs clean
	$(MAKE) -C udfinfo clean
	$(MAKE) -C udflabel clean

.PHONY: all install uninstall clean
