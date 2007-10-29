DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

SUBDIRS_EXT =

SUBDIRS = 			\
	gst-libs 		\
	gst $(SUBDIRS_EXT)	\
	tests 			\
	docs
#

#	docs			\
#	pkgconfig

DIST_SUBDIRS = 			\
	gst-libs		\
	gst $(SUBDIRS_EXT)	\
	tests 			\
	docs
#

#	docs			\
#	pkgconfig

EXTRA_DIST = \
	AUTHORS COPYING COPYING.LIB NEWS README RELEASE REQUIREMENTS \
	ChangeLog autogen.sh

ACLOCAL_AMFLAGS = -I common/m4

check-valgrind:
	cd tests/check && make check-valgrind

if HAVE_GST_CHECK
check-torture:
	cd tests/check && make torture
else
check-torture:
	true
endif
