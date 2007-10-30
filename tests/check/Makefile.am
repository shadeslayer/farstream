include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/tests/check/test-registry.xml

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY=$(CHECK_REGISTRY)

TESTS_ENVIRONMENT = \
	CK_DEFAULT_TIMEOUT=120					\
	STATE_IGNORE_ELEMENTS="$(STATE_IGNORE_ELEMENTS)"	\
	$(REGISTRY_ENVIRONMENT)					\
	GST_PLUGIN_SYSTEM_PATH=					\
	GST_PLUGIN_PATH=$(top_builddir)/gst


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
	base/fscodec


AM_CFLAGS = $(GST_CFLAGS) $(GST_CHECK_CFLAGS)
LDADD = $(GST_LIBS) $(GST_CHECK_LIBS)

base_fscodec_LDADD = $(LDADD) \
	$(top_builddir)/gst-libs/gst/farsight/libgstfarsight-0.10.la
base_fscodec_CFLAGS = $(FS2_PLUGINS_BASE_CFLAGS) $(CFLAGS) $(AM_CFLAGS)
