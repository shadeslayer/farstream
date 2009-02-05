plugin_LTLIBRARIES = libfsrtcpfilter.la

libfsrtcpfilter_la_SOURCES = fs-rtcp-filter.c

noinst_HEADERS = fs-rtcp-filter.h

libfsrtcpfilter_la_CFLAGS = \
	$(FS2_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libfsrtcpfilter_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libfsrtcpfilter_la_LIBADD = \
	$(FS2_LIBS) \
	-lgstrtp-@GST_MAJORMINOR@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)



fs-rtcp-filter-marshal.list: $(libfsrtcpfilter_la_SOURCES) Makefile.am
	( cd $(srcdir) && \
	sed -n -e 's/.*fs_rtcp-filter_marshal_\([[:upper:][:digit:]]*__[[:upper:][:digit:]_]*\).*/\1/p' \
	$(libfsrtcpfilter_la_SOURCES) ) \
	| sed -e 's/__/:/' -e 'y/_/,/' | sort -u > $@.tmp
	if cmp -s $@.tmp $@; then \
		rm $@.tmp; \
		touch $@; \
	else \
		mv $@.tmp $@; \
	fi

glib_enum_define=FS_RTCP_FILTER
glib_enum_prefix=_fs_rtcp_filter

include $(top_srcdir)/common-modified/glib-gen.mak

