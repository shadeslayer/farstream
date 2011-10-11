## Process this file with automake to produce Makefile.in

# FIXME: fix the docs then remove this variable
# DOCS_ARE_INCOMPLETE_PLEASE_FIXME=yespleasedo

# The name of the module, e.g. 'glib'.
#DOC_MODULE=gst-plugins-libs-@GST_MAJORMINOR@
DOC_MODULE=farstream-libs

# generated basefiles
basefiles = \
		$(DOC_MODULE).types \
		$(DOC_MODULE)-sections.txt \
		$(DOC_MODULE)-docs.sgml

# The top-level SGML file. Change it if you want.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The directory containing the source code. Relative to $(top_srcdir).
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting functions and macros.
DOC_SOURCE_DIR=$(top_srcdir)/gst-libs/gst
DOC_BUILD_DIR=$(top_builddir)/gst-libs/gst

# Extra options to supply to gtkdoc-scan.
SCAN_OPTIONS=

# FIXME :
# there's something wrong with gstreamer-sections.txt not being in the dist
# maybe it doesn't resolve; we're adding it below for now
#EXTRA_DIST = gstreamer.types.in gstreamer.hierarchy $(DOC_MODULE)-sections.txt gstreamer-sections.txt $(DOC_MAIN_SGML_FILE)

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode  --output-format=xml

# Extra options to supply to gtkdoc-fixref.
FIXXREF_OPTIONS=--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(GST_PREFIX)/share/gtk-doc/html

# Used for dependencies.
HFILE_GLOB=$(DOC_SOURCE_DIR)/*/*.h
CFILE_GLOB=$(DOC_SOURCE_DIR)/*/*.c

# this is a wingo addition
# thomasvs: another nice wingo addition would be an explanation on why
# this is useful ;)

SCANOBJ_DEPS = \
	$(top_builddir)/gst-libs/gst/farstream/libgstfarstream-@GST_MAJORMINOR@.la

# Header files to ignore when scanning.
IGNORE_HFILES = fs-marshal.h fs-enumtypes.h fs-private.h

# Images to copy into HTML directory.
HTML_IMAGES =

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files =

# Other files to distribute.
extra_files =

# CFLAGS and LDFLAGS for compiling scan program. Only needed if your app/lib
# contains GtkObjects/GObjects and you want to document signals and properties.
GTKDOC_CFLAGS = $(FS_INTERNAL_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS)
GTKDOC_LIBS = -static \
	$(top_builddir)/gst-libs/gst/farstream/libgstfarstream-0.10.la \
	$(GST_BASE_LIBS) \
	$(GCOV_LIBS)

GTKDOC_CC=$(LIBTOOL) --tag=CC --mode=compile $(CC)
GTKDOC_LD=$(LIBTOOL) --tag=CC --mode=link $(CC)

# If you need to override some of the declarations, place them in this file
# and uncomment this line.
#DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt
DOC_OVERRIDES =

include $(top_srcdir)/common/gtk-doc.mak
