PYDEFS=`pkg-config --variable=defsdir pygtk-2.0`
GSTPYDEFS=`pkg-config --variable=defsdir gst-python-0.10`

AM_CPPFLAGS =						\
	-I.						\
	-I$(top_srcdir)/gst-libs/gst/farsight/		\
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
	pyfarsightmodule.c				\
	pyfarsight.c

farsight_la_LIBADD =					\
	$(PYFARSIGHT_LIBS)				\
	$(top_builddir)/gst-libs/gst/farsight/libgstfarsight-@GST_MAJORMINOR@.la

farsight_la_LDFLAGS =					\
	`python-config --libs`				\
	-module -avoid-version

pyfarsight.c: pyfarsight.override pyfarsight.defs
	pygtk-codegen-2.0					\
		--prefix fs					\
		--register $(PYDEFS)/gdk-types.defs		\
		--register $(PYDEFS)/gtk-types.defs		\
		--register $(GSTPYDEFS)/gst-types.defs		\
		--override pyfarsight.override			\
		pyfarsight.defs > $@ 

EXTRA_DIST =			\
	pyfarsight.override	\
	pyfarsight.defs		\
	example.py

CLEANFILES = $(BUILT_SOURCES)
