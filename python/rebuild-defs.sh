#!/bin/sh

HEADERS=" \
    fs-codec.h \
    fs-candidate.h \
    fs-conference-iface.h \
    fs-session.h \
    fs-participant.h \
    fs-stream.h \
    fs-enum-types.h"

srcdir=../gst-libs/gst/farsight/

output=pyfarsight.defs
filter=pyfarsight-filter.defs

cat ${filter} > ${output}

for h in $HEADERS; do
    python /usr/share/pygtk/2.0/codegen/h2def.py \
	--defsfilter=${filter} ${srcdir}/$h >> $output
done
