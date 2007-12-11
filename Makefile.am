DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

SUBDIRS_EXT =

if WANT_PYTHON
PYTHON_SUBDIR = python
endif

SUBDIRS = 			\
	gst-libs 		\
	gst $(SUBDIRS_EXT)	\
	transmitters		\
	$(PYTHON_SUBDIR)	\
	tests 			\
	docs
#	pkgconfig

DIST_SUBDIRS = 			\
	gst-libs		\
	gst $(SUBDIRS_EXT)	\
	transmitters		\
	python			\
	tests 			\
	docs
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
