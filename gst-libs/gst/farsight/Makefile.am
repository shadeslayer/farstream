libgstfarsightincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/farsight

libgstfarsightinclude_HEADERS = \
		fs-base-conference.h \
		fs-candidate.h \
		fs-codec.h \
		fs-participant.h \
		fs-session.h \
		fs-stream.h \
		fs-conference-iface.h \
		fs-marshal.h

lib_LTLIBRARIES = libgstfarsight-@GST_MAJORMINOR@.la

BUILT_SOURCES = \
		fs-marshal.c \
		fs-marshal.h

libgstfarsight_@GST_MAJORMINOR@_la_SOURCES = \
		fs-base-conference.c \
		fs-candidate.c \
		fs-codec.c \
		fs-participant.c \
		fs-session.c \
		fs-stream.c \
		fs-conference-iface.c \
		fs-marshal.c

EXTRA_libgstfarsight_@GST_MAJORMINOR@_la_SOURCES = fs-marshal.list

fs-marshal.h: Makefile fs-marshal.list
		glib-genmarshal --header --prefix=fs_marshal $(srcdir)/fs-marshal.list > fs-marshal.h.tmp
		mv fs-marshal.h.tmp fs-marshal.h

fs-marshal.c: Makefile fs-marshal.list
		echo "#include \"glib-object.h\"" >> fs-marshal.c.tmp
		echo "#include \"fs-marshal.h\"" >> fs-marshal.c.tmp
		glib-genmarshal --body --prefix=fs_marshal $(srcdir)/fs-marshal.list >> fs-marshal.c.tmp
		mv fs-marshal.c.tmp fs-marshal.c

libgstfarsight_@GST_MAJORMINOR@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstfarsight_@GST_MAJORMINOR@_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)
libgstfarsight_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

noinst_HEADERS = fs-marshal.h
