include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/tests/check/test-registry.xml

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY=$(CHECK_REGISTRY)

TESTS_ENVIRONMENT = \
	G_SLICE=debug-blocks					\
	CK_DEFAULT_TIMEOUT=120					\
	STATE_IGNORE_ELEMENTS="$(STATE_IGNORE_ELEMENTS)"	\
	$(REGISTRY_ENVIRONMENT)					\
	GST_PLUGIN_LOADING_WHITELIST=gstreamer:gst-plugins-base:gst-plugins-good:libnice:valve:siren:autoconvert:rtpmux:dtmf:mimic:shm:farstream@$(top_builddir)/gst \
	GST_PLUGIN_PATH=$(top_builddir)/gst:${GST_PLUGIN_PATH}	\
	FS_PLUGIN_PATH=$(top_builddir)/transmitters/rawudp/.libs:$(top_builddir)/transmitters/multicast/.libs:$(top_builddir)/transmitters/nice/.libs:$(top_builddir)/transmitters/shm/.libs \
	LD_LIBRARY_PATH=$(top_builddir)/farstream/.libs:${LD_LIBRARY_PATH} \
	UPNP_XML_PATH=$(srcdir)/upnp \
	SRCDIR=$(srcdir) \
	XDG_CACHE_HOME=$(builddir)/cache


# ths core dumps of some machines have PIDs appended
CLEANFILES = core* test-registry.xml

clean-local: clean-local-check
	rm -rf cache

$(CHECK_REGISTRY):
	$(TESTS_ENVIRONMENT)


TESTS = $(check_PROGRAMS)

SUPPRESSIONS = $(top_srcdir)/common/gst.supp

EXTRA_DIST = \
	upnp/InternetGatewayDevice.xml \
	upnp/WANIPConnection.xml \
	base/test1.conf \
	utils/gstelements.conf

#######
# From here.. Its a list of our tests and their sub stuff
#
check_PROGRAMS = \
	base/fscodec \
	base/fstransmitter \
	transmitter/rawudp \
	transmitter/multicast \
	transmitter/nice \
	transmitter/shm \
	raw/conference \
	rtp/codecs \
	rtp/sendcodecs \
	rtp/conference \
	rtp/recvcodecs \
	msn/conference \
	utils/binadded \
	elements/rtcpfilter \
	elements/funnel

AM_CFLAGS = \
	$(CFLAGS) \
	$(FS_INTERNAL_CFLAGS) \
	$(FS_CFLAGS) \
	$(GST_CHECK_CFLAGS) \
	$(GST_CFLAGS)

LDADD = \
	$(top_builddir)/farstream/libfarstream-@FS_MAJORMINOR@.la \
	$(GST_CHECK_LIBS) \
	$(GST_LIBS)

base_fscodec_SOURCES = \
	testutils.c \
	testutils.h \
	base/fscodec.c


transmitter_rawudp_CFLAGS = $(AM_CFLAGS) $(GUPNP_CFLAGS) $(NICE_CFLAGS)
transmitter_rawudp_LDADD = $(LDADD) \
	$(GUPNP_LIBS) \
	$(NICE_LIBS)
transmitter_rawudp_SOURCES = \
	check-threadsafe.h  \
	testutils.c \
	testutils.h \
	transmitter/generic.c \
	transmitter/generic.h \
	transmitter/rawudp.c \
	transmitter/rawudp-upnp.c \
	transmitter/rawudp-upnp.h \
	transmitter/stunalternd.c \
	transmitter/stunalternd.h


transmitter_multicast_CFLAGS = $(AM_CFLAGS)
transmitter_multicast_SOURCES = \
	check-threadsafe.h  \
	testutils.c \
	testutils.h \
	transmitter/generic.c \
	transmitter/generic.h \
	transmitter/fake-filter.c \
	transmitter/fake-filter.h \
	transmitter/multicast.c
transmitter_multicast_LDADD = $(LDADD) $(GST_BASE_LIBS)

transmitter_nice_CFLAGS = $(FS_INTERNAL_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
transmitter_nice_SOURCES = \
	check-threadsafe.h  \
	transmitter/generic.c \
	transmitter/generic.h \
	transmitter/fake-filter.c \
	transmitter/fake-filter.h \
	transmitter/nice.c 
transmitter_nice_LDADD = $(LDADD) $(GST_BASE_LIBS)


transmitter_shm_CFLAGS = $(AM_CFLAGS) $(GUPNP_CFLAGS) $(NICE_CFLAGS)
transmitter_shm_LDADD = $(LDADD) \
	$(GUPNP_LIBS) \
	$(NICE_LIBS)
transmitter_shm_SOURCES = \
	check-threadsafe.h  \
	transmitter/generic.c \
	transmitter/generic.h \
	transmitter/shm.c

raw_conference_CFLAGS = $(AM_CFLAGS)
raw_conference_SOURCES = \
	check-threadsafe.h  \
	testutils.c \
	testutils.h \
	raw/conference.c

rtp_conference_CFLAGS = $(AM_CFLAGS)
rtp_conference_SOURCES = \
	check-threadsafe.h  \
	testutils.c \
	testutils.h \
	rtp/generic.c \
	rtp/generic.h \
	rtp/conference.c

rtp_codecs_CFLAGS = $(AM_CFLAGS)
rtp_codecs_SOURCES = \
	rtp/generic.c \
	rtp/generic.h \
	rtp/codecs.c

rtp_sendcodecs_CFLAGS = $(AM_CFLAGS)
rtp_sendcodecs_LDADD = $(LDADD) -lgstrtp-@GST_MAJORMINOR@
rtp_sendcodecs_SOURCES = \
	rtp/generic.c \
	rtp/generic.h \
	rtp/sendcodecs.c

rtp_recvcodecs_CFLAGS = $(AM_CFLAGS)
rtp_recvcodecs_LDADD = $(LDADD) -lgstrtp-@GST_MAJORMINOR@

msn_conference_CFLAGS = $(AM_CFLAGS)
msn_conference_SOURCES = \
	msn/conference.c

utils_binadded_CFLAGS = $(AM_CFLAGS)
utils_binadded_SOURCES = \
	testutils.c \
	testutils.h \
	utils/binadded.c

elements_rtcpfilter_CFLAGS = $(AM_CFLAGS)
elements_rtcpfilter_SOURCES = elements/rtcpfilter.c
elements_rtcpfilter_LDADD = $(LDADD) -lgstrtp-@GST_MAJORMINOR@

elements_funnel_CFLAGS = $(AM_CFLAGS)
elements_funnel_SOURCES = elements/funnel.c
