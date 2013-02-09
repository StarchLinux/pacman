BIN = vercmp testpkg testdb cleanupdelta pacsort pactree

.include "../Makefile.incl"

all: $(BIN)

$(BIN):
	$(CC) -o "$@" $(CFLAGS) $(LDFLAGS) "$@.c" $(LDADD)

install:
	install -Dm 755 $(BIN) $(DESTDIR)/bin/

clean:
	rm -f *.o $(BIN)
