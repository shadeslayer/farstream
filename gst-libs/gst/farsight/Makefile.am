libgstfarsightincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/farsight

libgstfarsightinclude_HEADERS = \
		fs-base-conference.h \
		fs-candidate.h \
		fs-codec.h \
		fs-participant.h \
		fs-session.h \
		fs-stream.h \
		fs-conference-iface.h \
		fs-transmitter.h \
		fs-stream-transmitter.h \
		fs-plugin.h \
		fs-marshal.h \
		fs-enum-types.h \
		fs-element-added-notifier.h

lib_LTLIBRARIES = libgstfarsight-@GST_MAJORMINOR@.la

BUILT_SOURCES = \
		fs-marshal.c \
		fs-marshal.h \
		fs-enum-types.c \
		fs-enum-types.h

CLEANFILES = $(BUILT_SOURCES)

libgstfarsight_@GST_MAJORMINOR@_la_SOURCES = \
		fs-base-conference.c \
		fs-candidate.c \
		fs-codec.c \
		fs-participant.c \
		fs-session.c \
		fs-stream.c \
		fs-conference-iface.c \
		fs-transmitter.c \
		fs-stream-transmitter.c \
		fs-plugin.c \
		fs-marshal.c \
		fs-enum-types.c \
		fs-element-added-notifier.c

noinst_HEADERS = \
	fs-marshal.h \
	fs-private.h

EXTRA_libgstfarsight_@GST_MAJORMINOR@_la_SOURCES = fs-marshal.list

fs-marshal.h: Makefile fs-marshal.list
		glib-genmarshal --header --prefix=_fs_marshal $(srcdir)/fs-marshal.list > fs-marshal.h.tmp
		mv fs-marshal.h.tmp fs-marshal.h

fs-marshal.c: Makefile fs-marshal.list
		echo "#include \"glib-object.h\"" >> fs-marshal.c.tmp
		echo "#include \"fs-marshal.h\"" >> fs-marshal.c.tmp
		glib-genmarshal --body --prefix=_fs_marshal $(srcdir)/fs-marshal.list >> fs-marshal.c.tmp
		mv fs-marshal.c.tmp fs-marshal.c

libgstfarsight_@GST_MAJORMINOR@_la_CFLAGS = $(FS2_INTERNAL_CFLAGS) $(FS2_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstfarsight_@GST_MAJORMINOR@_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)
libgstfarsight_@GST_MAJORMINOR@_la_LDFLAGS = $(FS2_LIB_LDFLAGS) $(FS2_ALL_LDFLAGS) $(FS2_LT_LDFLAGS) 


public_headers = fs-candidate.h \
		fs-codec.h \
		fs-participant.h \
		fs-session.h \
		fs-stream.h \
		fs-conference-iface.h

fs-enum-types.h: $(public_headers) Makefile
	( cd $(srcdir) && glib-mkenums \
			--fhead "#ifndef __FS_ENUM_TYPES_H__\n#define __FS_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
			--fprod "/* enumerations from \"@filename@\" */\n" \
			--vhead "GType @enum_name@_get_type (void);\n#define FS_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n" 	\
			--ftail "G_END_DECLS\n\n#endif /* __FS_ENUM_TYPES_H__ */" \
		$(public_headers) ) > fs-enum-types.h

fs-enum-types.c: $(public_headers) Makefile
	( cd $(srcdir) && glib-mkenums \
			--fhead "#include <fs-conference-iface.h>\n#include <fs-enum-types.h>" \
			--fprod "\n/* enumerations from \"@filename@\" */" \
			--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {" 	\
			--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
			--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
		$(public_headers) ) > fs-enum-types.c
