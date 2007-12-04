include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/tests/check/test-registry.xml

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY=$(CHECK_REGISTRY)

TESTS_ENVIRONMENT = \
	CK_DEFAULT_TIMEOUT=120					\
	STATE_IGNORE_ELEMENTS="$(STATE_IGNORE_ELEMENTS)"	\
	$(REGISTRY_ENVIRONMENT)					\
	GST_PLUGIN_SYSTEM_PATH=					\
	GST_PLUGIN_PATH=$(top_builddir)/gst:${GST_PLUGIN_PATH}	\
	FS_PLUGIN_PATH=$(top_builddir)/transmitters/rawudp/.libs


# ths core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.xml

clean-local: clean-local-check

$(CHECK_REGISTRY):
	$(TESTS_ENVIRONMENT)


TESTS = $(check_PROGRAMS)


SUPPRESSIONS = $(top_srcdir)/common/gst.supp

#######
# From here.. Its a list of our tests and their sub stuff
#
check_PROGRAMS = \
	base/fscodec \
	base/fstransmitter \
	transmitter/rawudp \
	main/rtpconference


AM_CFLAGS =  $(FS2_CFLAGS) $(GST_CFLAGS) $(GST_CHECK_CFLAGS)
LDADD = $(GST_LIBS) $(GST_CHECK_LIBS)

base_fscodec_LDADD = $(LDADD) \
	$(top_builddir)/gst-libs/gst/farsight/libgstfarsight-0.10.la
base_fscodec_CFLAGS = $(FS2_INTERNAL_CFLAGS) $(CFLAGS) $(AM_CFLAGS)

base_fstransmitter_LDADD = $(LDADD) \
	$(top_builddir)/gst-libs/gst/farsight/libgstfarsight-0.10.la
base_fstransmitter_CFLAGS = $(FS2_INTERNAL_CFLAGS) $(CFLAGS) $(AM_CFLAGS)

transmitter_rawudp_LDADD = $(LDADD) \
	$(top_builddir)/gst-libs/gst/farsight/libgstfarsight-0.10.la
transmitter_rawudp_CFLAGS = $(FS2_INTERNAL_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
transmitter_rawudp_SOURCES = \
	transmitter/generic.c \
	transmitter/generic.h \
	transmitter/rawudp.c



main_rtpconference_LDADD = $(LDADD) \
	$(top_builddir)/gst-libs/gst/farsight/libgstfarsight-0.10.la
main_rtpconference_CFLAGS = $(FS2_INTERNAL_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
main_rtpconference_SOURCES = \
	main/generic.c \
	main/generic.h \
	main/rtpconference.c

