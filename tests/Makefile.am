SUBDIRS_CHECK =

if HAVE_GST_CHECK
SUBDIRS_CHECK += check
endif

if HAVE_GUPNP
SUBDIRS_CHECK += upnp
endif

SUBDIRS = $(SUBDIRS_CHECK) rtp gui commandline

DIST_SUBDIRS = check rtp gui commandline upnp
