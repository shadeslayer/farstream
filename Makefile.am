DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc --enable-introspection

SUBDIRS_EXT =

if WANT_PYTHON
PYTHON_SUBDIR = python
endif

SUBDIRS = 			\
	farstream 		\
	gst $(SUBDIRS_EXT)	\
	transmitters		\
	$(PYTHON_SUBDIR)	\
	tests 			\
	docs			\
	common			\
	common-modified		\
	m4			\
	examples
#	pkgconfig

DIST_SUBDIRS = 			\
	farstream		\
	gst $(SUBDIRS_EXT)	\
	transmitters		\
	python			\
	tests 			\
	docs			\
	common			\
	common-modified		\
	m4			\
	examples
#	pkgconfig

EXTRA_DIST = \
	AUTHORS COPYING NEWS README ChangeLog autogen.sh \
	$(pcinfiles)

ACLOCAL_AMFLAGS = -I m4 -I common/m4

check-valgrind:
	cd tests/check && make check-valgrind

if HAVE_GST_CHECK
check-torture:
	cd tests/check && make torture
else
check-torture:
	true
endif

pcverfiles = farstream-@FS_MAJORMINOR@.pc
pcinfiles = farstream.pc.in

%-@FS_MAJORMINOR@.pc: %.pc
	cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(pcverfiles)

CLEANFILES = $(pcverfiles)

DISTCLEANFILES = $(pcinfiles:.in=)

dist-hook:
	chmod u+w ${distdir}/ChangeLog
	if test -d .git; then git log >${distdir}/ChangeLog; fi

include $(top_srcdir)/common/coverage/lcov.mak
