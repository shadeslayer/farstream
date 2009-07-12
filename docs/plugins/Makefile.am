GST_DOC_SCANOBJ = $(top_srcdir)/common/gstdoc-scangobj

## Process this file with automake to produce Makefile.in

# The name of the module, e.g. 'glib'.
#DOC_MODULE=gst-plugins-libs-@GST_MAJORMINOR@
MODULE=farsight2
DOC_MODULE=$(MODULE)-plugins

# generated basefiles
#basefiles = \
##		$(DOC_MODULE).types \
#		$(DOC_MODULE)-sections.txt \
#		$(DOC_MODULE)-docs.sgml

# ugly hack to make -unused.sgml work
#unused-build.stamp:
#	BUILDDIR=`pwd` && \
#	cd $(srcdir)/tmpl && \
#	ln -sf gstreamer-libs-unused.sgml \
#		$$BUILDDIR/tmpl/gstreamer-libs-@GST_MAJORMINOR@-unused.sgml
#	touch unused-build.stamp

# these rules are added to create parallel docs using GST_MAJORMINOR
#$(basefiles): gstreamer-libs-@GST_MAJORMINOR@%: gstreamer-libs%
#	cp $< $@

#CLEANFILES = $(basefiles)

# The top-level SGML file. Change it if you want.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The directory containing the source code. Relative to $(top_srcdir).
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting functions and macros.
DOC_SOURCE_DIR = $(top_srcdir)

# Extra options to supply to gtkdoc-scan.
SCAN_OPTIONS=

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode

# Extra options to supply to gtkdoc-fixref.
FIXXREF_OPTIONS=--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(GST_PREFIX)/share/gtk-doc/html \
        --extra-dir=$(datadir)/gtk-doc/html

# Used for dependencies.
HFILE_GLOB=$(DOC_SOURCE_DIR)/gst/*/*.h $(DOC_SOURCE_DIR)/transmitters/*/*.h
CFILE_GLOB=$(DOC_SOURCE_DIR)/gst/*/*.c $(DOC_SOURCE_DIR)/transmitters/*/*.c

# this is a wingo addition
# thomasvs: another nice wingo addition would be an explanation on why
# this is useful ;)

SCANOBJ_DEPS = \
	$(top_builddir)/transmitters/multicast/libmulticast-transmitter.la \
	$(top_builddir)/transmitters/rawudp/librawudp-transmitter.la \
	$(top_builddir)/transmitters/nice/libnice-transmitter.la \
	$(top_builddir)/gst/fsrtpconference/libfsrtpconference_doc.la \
	$(top_builddir)/gst/fsmsnconference/libfsmsnconference_doc.la \
	$(top_builddir)/gst/funnel/libfsfunnel.la \
	$(top_builddir)/gst/videoanyrate/libfsvideoanyrate.la 

# Header files to ignore when scanning.
IGNORE_HFILES = 
IGNORE_CFILES = 

# we add all .h files of elements that have signals/args we want
# sadly this also pulls in the private methods - maybe we should
# move those around in the source ?
# also, we should add some stuff here conditionally based on whether
# or not the plugin will actually build
# but I'm not sure about that - it might be this Just Works given that
# the registry won't have the element

EXTRA_HFILES = \
	$(top_srcdir)/gst/funnel/fs-funnel.h \
	$(top_srcdir)/gst/videoanyrate/videoanyrate.h \
	$(top_srcdir)/gst/fsrtpconference/fs-rtp-conference.h \
	$(top_srcdir)/gst/fsrtpconference/fs-rtp-session.h \
	$(top_srcdir)/gst/fsrtpconference/fs-rtp-stream.h \
	$(top_srcdir)/gst/fsrtpconference/fs-rtp-participant.h \
	$(top_srcdir)/gst/fsmsnconference/fs-msn-cam-recv-conference.h \
	$(top_srcdir)/gst/fsmsnconference/fs-msn-cam-send-conference.h \
	$(top_srcdir)/gst/fsmsnconference/fs-msn-conference.h \
	$(top_srcdir)/gst/fsmsnconference/fs-msn-session.h \
	$(top_srcdir)/gst/fsmsnconference/fs-msn-stream.h \
	$(top_srcdir)/gst/fsmsnconference/fs-msn-participant.h \
	$(top_srcdir)/transmitters/rawudp/fs-rawudp-transmitter.h \
	$(top_srcdir)/transmitters/rawudp/fs-rawudp-stream-transmitter.h \
	$(top_srcdir)/transmitters/multicast/fs-multicast-transmitter.h \
	$(top_srcdir)/transmitters/multicast/fs-multicast-stream-transmitter.h \
	$(top_srcdir)/transmitters/nice/fs-nice-transmitter.h \
	$(top_srcdir)/transmitters/nice/fs-nice-stream-transmitter.h

# Images to copy into HTML directory.
HTML_IMAGES =

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files =

# Other files to distribute.
extra_files = \
	farsight2-transmitters.types.in \
	gtkdoc-scangobj-transmitters

farsight2-transmitters.types: farsight2-transmitters.types.in Makefile
	cat $< > $@
	for i in "$(FS2_TRANSMITTER_PLUGINS_SELECTED)"; do \
		echo "${i}" >> $@ ;\
	done

CLEANFILES = farsight2-transmitters.types

# CFLAGS and LDFLAGS for compiling scan program. Only needed if your app/lib
# contains GtkObjects/GObjects and you want to document signals and properties.
GTKDOC_CFLAGS = $(GST_BASE_CFLAGS) -I$(top_builddir) -I$(top_builddir)/gst-libs
GTKDOC_LIBS = \
	$(top_builddir)/gst-libs/gst/farsight/libgstfarsight-0.10.la \
	$(top_builddir)/gst/fsrtpconference/libfsrtpconference_doc.la \
	$(top_builddir)/gst/fsmsnconference/libfsmsnconference_doc.la \
	$(GST_BASE_LIBS) 

GTKDOC_CC=$(LIBTOOL) --tag=CC --mode=compile $(CC)
GTKDOC_LD=$(LIBTOOL) --tag=CC --mode=link $(CC)

# If you need to override some of the declarations, place them in this file
# and uncomment this line.
#DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt
DOC_OVERRIDES =

FS_PLUGIN_PATH=$(top_builddir)/transmitters/rawudp/.libs:$(top_builddir)/transmitters/multicast/.libs:$(top_builddir)/transmitters/nice/.libs

update-all: scanobj-trans-build.stamp update


include $(top_srcdir)/common-modified/gtk-doc-plugins.mak
