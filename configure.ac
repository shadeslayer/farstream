AC_PREREQ(2.60)

dnl please read gstreamer/docs/random/autotools before changing this file

dnl initialize autoconf
dnl releases only do -Wall, cvs and prerelease does -Werror too
dnl use a three digit version number for releases, and four for cvs/prerelease
AC_INIT(Farsight2, 0.3.0.1,
    https://bugs.freedesktop.org/enter_bug.cgi?product=Farsight,
    farsight2)

AG_GST_INIT

dnl initialize automake
AM_INIT_AUTOMAKE

dnl define PACKAGE_VERSION_* variables
AS_VERSION

dnl check if this is a release version
AS_NANO(FS2_CVS="no", FS2_CVS="yes")

dnl can autoconf find the source ?
AC_CONFIG_SRCDIR([gst-libs/gst/farsight/fs-candidate.c])

dnl define the output header for config
AM_CONFIG_HEADER([config.h])

dnl sets host_* variables
AC_CANONICAL_HOST

dnl our libraries and install dirs use major.minor as a version
FS2_MAJORMINOR=$PACKAGE_VERSION_MAJOR.$PACKAGE_VERSION_MINOR
AC_SUBST(FS2_MAJORMINOR)

GST_MAJORMINOR=0.10
AC_SUBST(GST_MAJORMINOR)

dnl CURRENT, REVISION, AGE
dnl - library source changed -> increment REVISION
dnl - interfaces added/removed/changed -> increment CURRENT, REVISION = 0
dnl - interfaces added -> increment AGE
dnl - interfaces removed -> AGE = 0
dnl sets FS2_LT_LDFLAGS
AS_LIBTOOL(FS2, 0, 0, 0)

dnl FIXME: this macro doesn't actually work;
dnl the generated libtool script has no support for the listed tags.
dnl So this needs to be fixed first if we want to use this
dnl AS_LIBTOOL_TAGS

AM_PROG_LIBTOOL

dnl *** required versions of GStreamer stuff ***
GST_REQ=0.10.13.1
GSTPB_REQ=0.10.13.1

dnl *** autotools stuff ****

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE

dnl Add parameters for aclocal
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4 -I common/m4")

dnl *** check for arguments to configure ***

AG_GST_ARG_DEBUG
AG_GST_ARG_PROFILING
AG_GST_ARG_VALGRIND
AG_GST_ARG_GCOV

AG_GST_ARG_WITH_PKG_CONFIG_PATH
AG_GST_ARG_WITH_PACKAGE_NAME
AG_GST_ARG_WITH_PACKAGE_ORIGIN

dnl let distro override plugin install helper path
AC_ARG_WITH(install-plugins-helper,
  AC_HELP_STRING([--with-install-plugins-helper],
    [specify path of helper script to call to install plugins]),
  [
    case "${withval}" in
      yes) AC_MSG_ERROR(bad value ${withval} for --with-install-plugins-helper) ;;
      no)  AC_MSG_ERROR(bad value ${withval} for --with-install-plugins-helper) ;;
      *)   GST_INSTALL_PLUGINS_HELPER="${withval}" ;;
    esac
  ],
  [
    dnl Default value
    AS_AC_EXPAND(GST_INSTALL_PLUGINS_HELPER,${libexecdir}/gst-install-plugins-helper)
  ]
)
AC_MSG_NOTICE(Using $GST_INSTALL_PLUGINS_HELPER as plugin install helper)
AC_DEFINE_UNQUOTED(GST_INSTALL_PLUGINS_HELPER, "$GST_INSTALL_PLUGINS_HELPER",
    [plugin install helper script])
AC_SUBST(GST_INSTALL_PLUGINS_HELPER)

dnl these are all the gst plug-ins, compilable without additional libs
FS2_PLUGINS_ALL=" \
	fsrtpconference \
	"
AC_SUBST(FS2_PLUGINS_ALL)

FS2_PLUGINS_SELECTED=""

AC_ARG_WITH(plugins,
    AC_HELP_STRING([--with-plugins],
      [comma-separated list of plug-ins to compile]),
    [for i in `echo $withval | tr , ' '`; do
        if echo $FS2_PLUGINS_ALL | grep $i > /dev/null
        then
            FS2_PLUGINS_SELECTED="$FS2_PLUGINS_SELECTED $i"
        else
            echo "plug-in $i not recognized, ignoring..."
        fi
    done],
    [FS2_PLUGINS_SELECTED=$FS2_PLUGINS_ALL])

AC_SUBST(FS2_PLUGINS_SELECTED)

dnl ext plug-ins; plug-ins that have external dependencies
AG_GST_CHECK_FEATURE(EXTERNAL, [enable building of plug-ins with external deps],,
  [HAVE_EXTERNAL=yes], enabled,
  [
    AC_MSG_NOTICE(building external plug-ins)
    BUILD_EXTERNAL="yes"
  ],[
    AC_MSG_NOTICE(all plug-ins with external dependencies will not be built)
    BUILD_EXTERNAL="no"
  ])
AM_CONDITIONAL(BUILD_EXTERNAL, test "x$BUILD_EXTERNAL" = "xyes")

dnl *** checks for platform ***

dnl * hardware/architecture *

dnl *** checks for programs ***

dnl find a compiler
AC_PROG_CC

AC_PATH_PROG(VALGRIND_PATH, valgrind, no)
AM_CONDITIONAL(HAVE_VALGRIND, test ! "x$VALGRIND_PATH" = "xno")

dnl check for documentation tools
GTK_DOC_CHECK([1.3])
AS_PATH_PYTHON([2.1])
dnl AG_GST_PLUGIN_DOCS([1.3],[2.1])

dnl *** checks for libraries ***

dnl For interactive UNIX (a Sun thing)
dnl FIXME: this adds -lcposix to LIBS, but I doubt we use LIBS
AC_ISC_POSIX

dnl *** checks for header files ***

dnl check if we have ANSI C header files
AC_HEADER_STDC

dnl used in gst/ffmpegcolorspace/mem.c
dnl FIXME: could be fixed by redefining av_malloc and av_free to GLib's
AC_CHECK_HEADERS([malloc.h])

dnl *** checks for types/defines ***

dnl *** checks for structures ***

dnl *** checks for compiler characteristics ***

dnl *** checks for library functions ***

dnl Check for a way to display the function name in debug output
AG_GST_CHECK_FUNCTION

dnl *** checks for dependency libraries ***

dnl GLib is required
AG_GST_GLIB_CHECK([2.6])

dnl checks for gstreamer
dnl uninstalled is selected preferentially -- see pkg-config(1)
AG_GST_CHECK_GST($GST_MAJORMINOR, [$GST_REQ])
AG_GST_CHECK_GST_BASE($GST_MAJORMINOR, [$GST_REQ])
AG_GST_CHECK_GST_GDP($GST_MAJORMINOR, [$GST_REQ])
AG_GST_CHECK_GST_CONTROLLER($GST_MAJORMINOR, [$GST_REQ])
AG_GST_CHECK_GST_CHECK($GST_MAJORMINOR, [$GST_REQ], no)
AG_GST_CHECK_GST_PLUGINS_BASE($GST_MAJORMINOR, [$GSTPB_REQ])
AM_CONDITIONAL(HAVE_GST_CHECK, test "x$HAVE_GST_CHECK" = "xyes")

dnl Check for documentation xrefs
GLIB_PREFIX="`$PKG_CONFIG --variable=prefix glib-2.0`"
FS2_PREFIX="`$PKG_CONFIG --variable=prefix farsight-$FS2_MAJORMINOR`"
AC_SUBST(GLIB_PREFIX)
AC_SUBST(FS2_PREFIX)

dnl *** set variables based on configure arguments ***

dnl set license and copyright notice
FS2_LICENSE="LGPL"
AC_DEFINE_UNQUOTED(FS2_LICENSE, "$FS2_LICENSE", [Farsight2 license])
AC_SUBST(FS2_LICENSE)

dnl set location of plugin directory
AG_GST_SET_PLUGINDIR

dnl define an ERROR_CFLAGS Makefile variable
AG_GST_SET_ERROR_CFLAGS($FS2_CVS)

dnl define correct level for debugging messages
AG_GST_SET_LEVEL_DEFAULT($FS2_CVS)

dnl *** sys plug-ins ***

echo
AC_MSG_NOTICE([Checking libraries for plugins in sys/])
echo

dnl *** ext plug-ins ***
dnl keep this list sorted alphabetically !

if test "x$BUILD_EXTERNAL" = "xyes"; then

echo
AC_MSG_NOTICE([Checking libraries for plugins in ext/])
echo

else

dnl not building plugins with external dependencies,
dnl but we still need to set the conditionals
echo

fi dnl of EXT plugins

dnl *** finalize CFLAGS, LDFLAGS, LIBS

dnl Overview:
dnl FS2_OPTION_CFLAGS:  common flags for profiling, debugging, errors, ...
dnl GST_*:              flags shared by built objects to link against GStreamer
dnl FS2_PLUGINS_BASE_CFLAGS: to link internally against the plugins base libs
dnl                          (compare to other modules) or for i18n
dnl FS2_ALL_LDFLAGS:    linker flags shared by all
dnl FS2_LIB_LDFLAGS:    additional linker flags for all libaries
dnl FS2_LT_LDFLAGS:     library versioning of our libraries
dnl FS2_PLUGIN_LDFLAGS: flags to be used for all plugins

dnl FS2_OPTION_CFLAGS
if test "x$USE_DEBUG" = xyes; then
   PROFILE_CFLAGS="-g"
fi
AC_SUBST(PROFILE_CFLAGS)

dnl every flag in FS2_OPTION_CFLAGS can be overridden at make time
FS2_OPTION_CFLAGS="\$(ERROR_CFLAGS) \$(DEBUG_CFLAGS) \$(PROFILE_CFLAGS) \$(GCOV_CFLAGS) \$(OPT_CFLAGS)"
AC_SUBST(FS2_OPTION_CFLAGS)

dnl our libraries need to be versioned correctly
AC_SUBST(FS2_LT_LDFLAGS)

dnl FS2_PLUGINS_BASE_CFLAGS
dnl prefer internal headers to already installed ones
dnl also add builddir include for enumtypes and marshal
FS2_PLUGINS_BASE_CFLAGS="-I\$(top_srcdir)/gst-libs -I\$(top_builddir)/gst-libs"
AC_SUBST(FS2_PLUGINS_BASE_CFLAGS)

dnl FIXME: do we want to rename to GST_ALL_* ?
dnl add FS2_OPTION_CFLAGS, but overridable
FS2_CFLAGS="$GST_CFLAGS \$(FS2_OPTION_CFLAGS)"
AC_SUBST(FS2_CFLAGS)

dnl add GCOV libs because libtool strips -fprofile-arcs -ftest-coverage
FS2_LIBS="$GST_LIBS \$(GCOV_LIBS)"
AC_SUBST(FS2_LIBS)

dnl LDFLAGS really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
FS2_ALL_LDFLAGS="-no-undefined"
AC_SUBST(FS2_ALL_LDFLAGS)

dnl FS2_LIB_LDFLAGS
dnl linker flags shared by all libraries
dnl LDFLAGS modifier defining exported symbols from built libraries
FS2_LIB_LDFLAGS="-export-symbols-regex [_]*\(fs_\|Fs\|FS_\).*"
AC_SUBST(FS2_LIB_LDFLAGS)

dnl this really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
FS2_PLUGIN_LDFLAGS="-module -avoid-version -export-symbols-regex '^[_]*gst_plugin_desc\$\$' $GST_ALL_LDFLAGS"
AC_SUBST(FS2_PLUGIN_LDFLAGS)

dnl *** output files ***

AC_CONFIG_FILES(
Makefile
gst/Makefile
gst/fsrtpconference/Makefile
gst-libs/Makefile
gst-libs/gst/Makefile
gst-libs/gst/farsight/Makefile
dnl pkgconfig/Makefile
dnl pkgconfig/farsight2.pc
dnl pkgconfig/farsight2-uninstalled.pc
tests/Makefile
tests/check/Makefile
docs/Makefile
docs/libs/Makefile
docs/version.entities
dnl docs/plugins/Makefile
)
AC_OUTPUT

AG_GST_OUTPUT_PLUGINS
