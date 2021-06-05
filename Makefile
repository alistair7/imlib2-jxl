CPPFLAGS += -DIMLIB2JXL_USE_LCMS
RELEASE_CFLAGS ?= -O3 -march=native
DEBUG_CFLAGS ?= -O0 -g
SHARED_CFLAGS := -Wall -Wextra `imlib2-config --cflags` `pkg-config lcms2 --cflags` -fPIC
LDFLAGS += `imlib2-config --libs` -ljxl_threads -ljxl `pkg-config lcms2 --libs`

.PHONY: clean distclean debug install-debug release install-release install

release: jxl.so
debug: jxl-dbg.so

jxl.so: imlib2-jxl.o
	$(CC) -shared -o$@ $^ $(LDFLAGS)

imlib2-jxl.o: imlib2-jxl.c
	$(CC) -c $(CPPFLAGS) $(SHARED_CFLAGS) $(RELEASE_CFLAGS) -o$@ $<

install-release: install
install: jxl.so
	install -m 644 $< `pkg-config imlib2 --variable=libdir`/imlib2/loaders/

clean:
	$(RM) imlib2-jxl.o imlib2-jxl-dbg.o

distclean: clean
	$(RM) jxl.so jxl-dbg.so


jxl-dbg.so: imlib2-jxl-dbg.o
	$(CC) -shared -o$@ $^ $(LDFLAGS)

imlib2-jxl-dbg.o: imlib2-jxl.c
	$(CC) -c $(CPPFLAGS) -DIMLIB2JXL_DEBUG $(SHARED_CFLAGS) $(DEBUG_CFLAGS) -o$@ $<

install-debug: jxl-dbg.so
	install -m 644 $< `pkg-config imlib2 --variable=libdir`/imlib2/loaders/
