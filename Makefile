CFLAGS ?= -O3 -march=native -Wall -Wextra
CFLAGS += `imlib2-config --cflags` -fPIC
LDFLAGS += `imlib2-config --libs` -ljxl_threads -ljxl

.PHONY: clean distclean install

jxl.so: imlib2-jxl.o
	$(CC) -shared -o$@ $^ $(LDFLAGS)

imlib2-jxl.o: imlib2-jxl.c
	$(CC) -c $(CFLAGS) -o$@ $<

install:
	./install.sh

clean:
	$(RM) imlib2-jxl.o

distclean: clean
	$(RM) jxl.so
