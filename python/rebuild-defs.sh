#!/bin/sh

HEADERS=" \
    fs-codec.h \
    fs-candidate.h \
    fs-conference-iface.h \
    fs-session.h \
    fs-participant.h \
    fs-stream.h \
    fs-element-added-notifier.h \
    fs-enum-types.h"

srcdir=../gst-libs/gst/farsight/

output=pyfarsight.defs
filter=pyfarsight-filter.defs

cat ${filter} > ${output}

H2DEF=
[ -z "${H2DEF}" -a -f /usr/share/pygtk/2.0/codegen/h2def.py ] && H2DEF=/usr/share/pygtk/2.0/codegen/h2def.py
[ -z "${H2DEF}" -a -f /usr/lib/python2.5/site-packages/gtk-2.0/codegen/h2def.py ] && H2DEF=/usr/lib/python2.5/site-packages/gtk-2.0/codegen/h2def.py
[ -z "${H2DEF}" -a -f /usr/lib/python2.4/site-packages/gtk-2.0/codegen/h2def.py ] && H2DEF=/usr/lib/python2.4/site-packages/gtk-2.0/codegen/h2def.py

for h in $HEADERS; do
    python ${H2DEF} --defsfilter=${filter} ${srcdir}/$h >> $output
done

sed -e "/of-object \"FsSession\"/ a \
      \  (unblock-threads t)" \
    -e "/of-object \"FsStream\"/ a \
      \  (unblock-threads t)" \
    -e "/of-object \"FsConference\"/ a \
      \  (unblock-threads t)" \
    -i pyfarsight.defs
