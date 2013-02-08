FILES = makepkg.conf pacman.conf

all: $(FILES) Makefile

install: all
	install -Dm 644 $(FILES) $(DESTDIR)/etc/
