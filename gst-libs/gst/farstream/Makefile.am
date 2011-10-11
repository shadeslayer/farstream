libfarstreamincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/farstream

libfarstreaminclude_HEADERS = \
		fs-candidate.h \
		fs-codec.h \
		fs-participant.h \
		fs-session.h \
		fs-stream.h \
		fs-conference.h \
		fs-transmitter.h \
		fs-stream-transmitter.h \
		fs-plugin.h \
		fs-element-added-notifier.h \
		fs-utils.h \
		fs-rtp.h

nodist_libfarstreaminclude_HEADERS = \
		fs-enumtypes.h


lib_LTLIBRARIES = libfarstream-@GST_MAJORMINOR@.la

BUILT_SOURCES = \
	$(nodist_libfarstream_@GST_MAJORMINOR@_la_SOURCES) \
	$(nodist_libfarstreaminclude_HEADERS)

CLEANFILES = $(BUILT_SOURCES) fs-marshal.list

libfarstream_@GST_MAJORMINOR@_la_SOURCES = \
		fs-candidate.c \
		fs-codec.c \
		fs-participant.c \
		fs-session.c \
		fs-stream.c \
		fs-conference.c \
		fs-transmitter.c \
		fs-stream-transmitter.c \
		fs-plugin.c \
		fs-element-added-notifier.c \
		fs-utils.c \
		fs-rtp.c \
		fs-private.h

nodist_libfarstream_@GST_MAJORMINOR@_la_SOURCES = \
		fs-marshal.c \
		fs-marshal.h \
		fs-enumtypes.c


fs-marshal.list: $(libfarstream_@GST_MAJORMINOR@_la_SOURCES) Makefile.am
	$(AM_V_GEN) ( cd $(srcdir) && \
	sed -n -e 's/.*_fs_marshal_\([[:upper:][:digit:]]*__[[:upper:][:digit:]_]*\).*/\1/p' \
	$(libfarstream_@GST_MAJORMINOR@_la_SOURCES) ) \
	| sed -e 's/__/:/' -e 'y/_/,/' | sort -u > $@.tmp
	@if cmp -s $@.tmp $@; then \
		rm $@.tmp; \
		touch $@; \
	else \
		mv $@.tmp $@; \
	fi

libfarstream_@GST_MAJORMINOR@_la_CFLAGS = \
	$(FS_INTERNAL_CFLAGS) $(FS_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libfarstream_@GST_MAJORMINOR@_la_LIBADD = \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libfarstream_@GST_MAJORMINOR@_la_LDFLAGS = \
	$(FS_LIB_LDFLAGS) \
	$(FS_ALL_LDFLAGS) \
	$(FS_LT_LDFLAGS) 


public_headers = fs-candidate.h \
		fs-codec.h \
		fs-participant.h \
		fs-session.h \
		fs-stream.h \
		fs-conference.h \
		fs-utils.h

glib_enum_headers=$(public_headers)
glib_enum_define=FS
glib_gen_prefix=_fs
glib_gen_basename=fs

include $(top_srcdir)/common-modified/gst-glib-gen.mak

if HAVE_INTROSPECTION
include $(INTROSPECTION_MAKEFILE)
introspection_sources = \
	$(libfarstream_@GST_MAJORMINOR@_la_SOURCES) \
	$(nodist_libfarstreaminclude_HEADERS) \
	$(libfarstreaminclude_HEADERS)

INTROSPECTION_GIRS = Farstream-@FS_MAJORMINOR@.gir
Farstream_0_1_gir_NAMESPACE = Farstream
Farstream_0_1_gir_VERSION = @FS_MAJORMINOR@
Farstream_0_1_gir_LIBS = libfarstream-@GST_MAJORMINOR@.la
Farstream_0_1_gir_FILES = $(introspection_sources)
Farstream_0_1_gir_INCLUDES = GObject-2.0 Gst-0.10
Farstream_0_1_gir_CFLAGS = $(FS_INTERNAL_CFLAGS)
Farstream_0_1_gir_SCANNERFLAGS = --identifier-prefix=fs_ --identifier-prefix=Fs

girdir = $(datadir)/gir-1.0
dist_gir_DATA = Farstream-@FS_MAJORMINOR@.gir
typelibdir = $(libdir)/girepository-1.0
typelib_DATA = Farstream-@FS_MAJORMINOR@.typelib
CLEANFILES += $(dist_gir_DATA) $(typelib_DATA)
endif
