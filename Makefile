
all: test test-thread

CFLAGS = -g -Wextra -Wall -Wno-unused-parameter -Wno-missing-field-initializers `pkg-config --cflags gupnp-1.0`
LDFLAGS =  `pkg-config --libs gupnp-1.0`

fs-upnp-simple-igd.o: fs-upnp-simple-igd-marshal.h fs-upnp-simple-igd.c fs-upnp-simple-igd.h

fs-upnp-simple-igd-thread.o: fs-upnp-simple-igd-thread.c fs-upnp-simple-igd-thread.h

test: fs-upnp-simple-igd-marshal.o fs-upnp-simple-igd.o test.o 

test-thread: fs-upnp-simple-igd-thread.o fs-upnp-simple-igd-marshal.o fs-upnp-simple-igd.o test-thread.o 

clean:
	rm -f *.o test test-thread *-marshal.[ch]

SOURCES = fs-upnp-simple-igd.c
srcdir = .

fs-upnp-simple-igd-marshal.h: fs-upnp-simple-igd-marshal.list Makefile
	glib-genmarshal --header --prefix=_fs_upnp_simple_igd_marshal $(srcdir)/$< > $@.tmp
	mv $@.tmp $@

fs-upnp-simple-igd-marshal.c: fs-upnp-simple-igd-marshal.list Makefile
	echo "#include \"glib-object.h\"" >> $@.tmp
	echo "#include \"fs-upnp-simple-igd-marshal.h\"" >> $@.tmp
	glib-genmarshal --body --prefix=_fs_upnp_simple_igd_marshal $(srcdir)/$< >> $@.tmp
	mv $@.tmp $@
