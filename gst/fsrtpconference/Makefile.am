plugin_LTLIBRARIES = libfsrtpconference.la

# First build a convenience lib with the common stuff so its not built twice

noinst_LTLIBRARIES = libfsrtpconference-convenience.la

libfsrtpconference_convenience_la_SOURCES = \
	fs-rtp-conference.c \
	fs-rtp-participant.c \
	fs-rtp-session.c \
	fs-rtp-stream.c \
	fs-rtp-substream.c \
	fs-rtp-discover-codecs.c \
	fs-rtp-codec-cache.c \
	fs-rtp-codec-negotiation.c \
	fs-rtp-codec-specific.c \
	fs-rtp-special-source.c \
	fs-rtp-dtmf-event-source.c \
	fs-rtp-dtmf-sound-source.c \
	fs-rtp-bitrate-adapter.c \
	fs-rtp-keyunit-manager.c \
	fs-rtp-tfrc.c \
	fs-rtp-packet-modder.c \
	tfrc.c

nodist_libfsrtpconference_convenience_la_SOURCES = \
	fs-rtp-marshal.c \
	fs-rtp-marshal.h

BUILT_SOURCES = $(nodist_libfsrtpconference_convenience_la_SOURCES)

noinst_HEADERS = \
	fs-rtp-conference.h \
	fs-rtp-participant.h \
	fs-rtp-session.h \
	fs-rtp-stream.h \
	fs-rtp-substream.h \
	fs-rtp-discover-codecs.h \
	fs-rtp-codec-cache.h \
	fs-rtp-codec-negotiation.h \
	fs-rtp-codec-specific.h \
	fs-rtp-special-source.h \
	fs-rtp-dtmf-event-source.h \
	fs-rtp-dtmf-sound-source.h \
	fs-rtp-bitrate-adapter.h \
	fs-rtp-keyunit-manager.h \
	fs-rtp-tfrc.h \
	fs-rtp-packet-modder.h \
	tfrc.h

CLEANFILES = $(BUILT_SOURCES) fs-rtp-marshal.list

AM_CFLAGS = \
	$(FS_INTERNAL_CFLAGS) \
	$(FS_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS)

# Build the main plugin

libfsrtpconference_la_SOURCES = fs-rtp-conference-plugin.c

libfsrtpconference_la_LIBADD = \
	libfsrtpconference-convenience.la \
	$(top_builddir)/farstream/libfarstream-@FS_MAJORMINOR@.la \
	$(FS_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_LIBS) \
	-lgstrtp-@GST_MAJORMINOR@ \
	-lm

libfsrtpconference_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

preferencesdir = $(datadir)/$(PACKAGE_TARNAME)/$(FS_MAJORMINOR)/fsrtpconference
preferences_DATA = \
	default-codec-preferences \
	default-element-properties

EXTRA_DIST = $(preferences_DATA)

# Optionally build a library to allow building the gtk-doc

if ENABLE_GTK_DOC
noinst_LTLIBRARIES += libfsrtpconference_doc.la

nodist_libfsrtpconference_doc_la_SOURCES = \
	fs-rtp-conference-plugin.doc.c  \
	$(nodist_libfsrtpconference_la_SOURCES)
libfsrtpconference_doc_la_LIBADD = $(libfsrtpconference_la_LIBADD)

%.doc.c: %.c
	$(AM_V_GEN)sed -e 's:GST_PLUGIN_DEFINE:GST_PLUGIN_DEFINE_STATIC:' $< > $@

BUILT_SOURCES += fs-rtp-conference-plugin.doc.c
endif

# Build the build sources

fs-rtp-marshal.list: $(libfsrtpconference_convenience_la_SOURCES) Makefile.am
	$(AM_V_GEN)( cd $(srcdir) && \
	sed -n -e 's/.*fs_rtp_marshal_\([[:upper:][:digit:]]*__[[:upper:][:digit:]_]*\).*/\1/p' \
	$(libfsrtpconference_convenience_la_SOURCES) ) \
	| sed -e 's/__/:/' -e 'y/_/,/' | sort -u > $@.tmp
	@if cmp -s $@.tmp $@; then \
		rm $@.tmp; \
		touch $@; \
	else \
		mv $@.tmp $@; \
	fi

glib_enum_define=FS_RTP
glib_gen_prefix=_fs_rtp
glib_gen_basename=fs-rtp

include $(top_srcdir)/common/gst-glib-gen.mak
