PYDEFS=`pkg-config --variable=defsdir pygobject-2.0`
GSTPYDEFS=`pkg-config --variable=defsdir gst-python-0.10`

AM_CPPFLAGS =						\
	-I.						\
	-I$(top_srcdir)/gst-libs/			\
	-DDATADIR=\""$(datadir)"\"			\
	$(PYFARSIGHT_CFLAGS)				\
	$(PYTHON_INCLUDES)				\
	$(FS2_INTERNAL_CFLAGS) 				\
	$(FS2_CFLAGS) 					\
	$(WARN_CFLAGS)

BUILT_SOURCES =						\
	pyfarsight.c					 

pyfarsightdir = $(pyexecdir)
pyfarsight_LTLIBRARIES = farsight.la

farsight_la_SOURCES =					\
	pyfarsightmodule.c

nodist_farsight_la_SOURCES = pyfarsight.c

farsight_la_LIBADD =					\
	$(PYFARSIGHT_LIBS)				\
	$(top_builddir)/gst-libs/gst/farsight/libgstfarsight-@GST_MAJORMINOR@.la

farsight_la_LDFLAGS =					\
	`python-config --libs`				\
	-module -avoid-version

pyfarsight.c: pyfarsight.override pyfarsight.defs
	$(PYCODEGEN)						\
		--prefix fs					\
		--register $(GSTPYDEFS)/gst-types.defs		\
		--override $(srcdir)/pyfarsight.override	\
		$(srcdir)/pyfarsight.defs > $@ 

EXTRA_DIST =			\
	pyfarsight.override	\
	pyfarsight.defs		\
	example.py		\
	pyfarsight-filter.defs  \
	rebuild-defs.sh

CLEANFILES = $(BUILT_SOURCES)
