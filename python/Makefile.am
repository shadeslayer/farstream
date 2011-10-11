PYDEFS=`pkg-config --variable=defsdir pygobject-2.0`
GSTPYDEFS=`pkg-config --variable=defsdir gst-python-0.10`

AM_CPPFLAGS =						\
	-I.						\
	-I$(top_srcdir)/gst-libs/			\
	-DDATADIR=\""$(datadir)"\"			\
	$(PYFARSTREAM_CFLAGS)				\
	$(PYTHON_INCLUDES)				\
	$(FS_INTERNAL_CFLAGS) 				\
	$(FS_CFLAGS) 					\
	$(WARN_CFLAGS)

BUILT_SOURCES =						\
	pyfarstream.c					 

pyfarstreamdir = $(pyexecdir)
pyfarstream_LTLIBRARIES = farstream.la

farstream_la_SOURCES =					\
	pyfarstreammodule.c

nodist_farstream_la_SOURCES = pyfarstream.c

farstream_la_LIBADD =					\
	$(PYFARSTREAM_LIBS)				\
	$(top_builddir)/farstream/libfarstream-@GST_MAJORMINOR@.la

farstream_la_LDFLAGS = -module -avoid-version

pyfarstream.c: pyfarstream.override pyfarstream.defs
	$(PYCODEGEN)						\
		--prefix fs					\
		--register $(GSTPYDEFS)/gst-types.defs		\
		--override $(srcdir)/pyfarstream.override	\
		$(srcdir)/pyfarstream.defs > $@ 

EXTRA_DIST =			\
	pyfarstream.override	\
	pyfarstream.defs		\
	example.py		\
	pyfarstream-filter.defs  \
	rebuild-defs.sh

CLEANFILES = $(BUILT_SOURCES)
