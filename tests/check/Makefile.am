include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/tests/check/test-registry.xml

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY=$(CHECK_REGISTRY)

TESTS_ENVIRONMENT = \
	CK_DEFAULT_TIMEOUT=120					\
	STATE_IGNORE_ELEMENTS="$(STATE_IGNORE_ELEMENTS)"	\
	$(REGISTRY_ENVIRONMENT)					\
	GST_PLUGIN_PATH=$(top_builddir)/gst:${GST_PLUGIN_PATH}	\
	FS_PLUGIN_PATH=$(top_builddir)/transmitters/rawudp/.libs:$(top_builddir)/transmitters/multicast/.libs


# ths core dumps of some machines have PIDs appended
CLEANFILES = core* test-registry.xml

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
	transmitter/multicast \
	main/rtpconference \
	main/rtpcodecs \
	main/rtpsendcodecs \
	utils/binadded


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
	check-threadsafe.h  \
	transmitter/generic.c \
	transmitter/generic.h \
	transmitter/rawudp.c 

transmitter_multicast_LDADD = $(LDADD) \
	$(top_builddir)/gst-libs/gst/farsight/libgstfarsight-0.10.la
transmitter_multicast_CFLAGS = $(FS2_INTERNAL_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
transmitter_multicast_SOURCES = \
	check-threadsafe.h  \
	transmitter/generic.c \
	transmitter/generic.h \
	transmitter/multicast.c 



main_rtpconference_LDADD = $(LDADD) \
	$(top_builddir)/gst-libs/gst/farsight/libgstfarsight-0.10.la
main_rtpconference_CFLAGS = $(FS2_INTERNAL_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
main_rtpconference_SOURCES = \
	check-threadsafe.h  \
	main/generic.c \
	main/generic.h \
	main/rtpconference.c


main_rtpcodecs_LDADD = $(LDADD) \
	$(top_builddir)/gst-libs/gst/farsight/libgstfarsight-0.10.la
main_rtpcodecs_CFLAGS = $(FS2_INTERNAL_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
main_rtpcodecs_SOURCES = \
	main/generic.c \
	main/generic.h \
	main/rtpcodecs.c


main_rtpsendcodecs_LDADD = $(LDADD) \
	$(top_builddir)/gst-libs/gst/farsight/libgstfarsight-0.10.la \
	-lgstrtp-0.10
main_rtpsendcodecs_CFLAGS = $(FS2_INTERNAL_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
main_rtpsendcodecs_SOURCES = \
	main/generic.c \
	main/generic.h \
	main/rtpsendcodecs.c


utils_binadded_LDADD = $(LDADD) \
	$(top_builddir)/gst-libs/gst/farsight/libgstfarsight-0.10.la
utils_binadded_CFLAGS = $(FS2_INTERNAL_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
utils_binadded_SOURCES = \
	utils/binadded.c

