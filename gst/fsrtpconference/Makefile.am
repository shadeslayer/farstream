plugin_LTLIBRARIES = libfsrtpconference.la

common_SOURCES = \
	fs-rtp-conference.c \
	fs-rtp-participant.c \
	fs-rtp-session.c \
	fs-rtp-stream.c \
	fs-rtp-substream.c \
	fs-rtp-discover-codecs.c \
	fs-rtp-codec-cache.c \
	fs-rtp-codec-negotiation.c \
	fs-rtp-specific-nego.c \
	fs-rtp-special-source.c \
	fs-rtp-dtmf-event-source.c \
	fs-rtp-dtmf-sound-source.c \
	fs-rtp-marshal.c
libfsrtpconference_la_SOURCES = gstfsrtpconference.c $(common_SOURCES)

BUILT_SOURCES = \
		fs-rtp-marshal.c \
		fs-rtp-marshal.h \
		gstfsrtpconference_doc.c

noinst_HEADERS = \
	fs-rtp-conference.h \
	fs-rtp-participant.h \
	fs-rtp-session.h \
	fs-rtp-stream.h \
	fs-rtp-substream.h \
	fs-rtp-discover-codecs.h \
	fs-rtp-codec-cache.h \
	fs-rtp-codec-negotiation.h \
	fs-rtp-specific-nego.h \
	fs-rtp-special-source.h \
	fs-rtp-dtmf-event-source.h \
	fs-rtp-dtmf-sound-source.h \
	fs-rtp-marshal.h

EXTRA_libfsrtpconference_la_SOURCES = fs-rtp-marshal.list

CLEANFILES = $(BUILT_SOURCES)

libfsrtpconference_la_CFLAGS = \
	$(FS2_INTERNAL_CFLAGS) \
	$(FS2_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS)
libfsrtpconference_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libfsrtpconference_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/farsight/libgstfarsight-0.10.la \
	$(FS2_LIBS) \
	-lgstrtp-@GST_MAJORMINOR@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)


if ENABLE_GTK_DOC
noinst_LTLIBRARIES = libfsrtpconference_doc.la

libfsrtpconference_doc_la_SOURCES = gstfsrtpconference_doc.c $(common_SOURCES)
libfsrtpconference_doc_la_CFLAGS = $(libfsrtpconference_la_CFLAGS)
libfsrtpconference_doc_la_LIBADD = $(libfsrtpconference_la_LIBADD)

gstfsrtpconference_doc.c: gstfsrtpconference.c
	sed -e 's:GST_PLUGIN_DEFINE:GST_PLUGIN_DEFINE_STATIC:' \
		gstfsrtpconference.c > gstfsrtpconference_doc.c
endif

fs-rtp-marshal.h: fs-rtp-marshal.list Makefile
		glib-genmarshal --header --prefix=_fs_rtp_marshal $(srcdir)/$< > $@.tmp
		mv $@.tmp $@

fs-rtp-marshal.c: fs-rtp-marshal.list Makefile
		echo "#include \"glib-object.h\"" >> $@.tmp
		echo "#include \"fs-rtp-marshal.h\"" >> $@.tmp
		glib-genmarshal --body --prefix=_fs_rtp_marshal $(srcdir)/$< >> $@.tmp
		mv $@.tmp $@
