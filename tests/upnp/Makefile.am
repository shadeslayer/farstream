
noinst_PROGRAMS = test test-thread


LDADD = \
	$(top_builddir)/gst-libs/ext/fsupnp/libfsupnp.la \
	$(GUPNP_LIBS) 

test_CFLAGS = $(GUPNP_CFLAGS) \
	-I$(top_srcdir)/gst-libs

test_thread_CFLAGS = $(GUPNP_CFLAGS) \
	-I$(top_srcdir)/gst-libs
