#!/bin/sh

HEADERS=" \
    fs-codec.h \
    fs-candidate.h \
    fs-conference.h \
    fs-session.h \
    fs-participant.h \
    fs-stream.h \
    fs-rtp.h \
    fs-element-added-notifier.h \
    fs-enumtypes.h"

srcdir=../gst-libs/gst/farsight/

output=pyfarsight.defs
filter=pyfarsight-filter.defs

cat ${filter} > ${output}



H2DEF="$(pkg-config --variable=codegendir pygobject-2.0)/h2def.py"
[ -z "${H2DEF}" ] && H2DEF="$(pkg-config --variable=codegendir pygtk-2.0)/h2def.py"
[ -z "${H2DEF}" -a -f /usr/share/pygtk/2.0/codegen/h2def.py ] && H2DEF=/usr/share/pygtk/2.0/codegen/h2def.py

for h in $HEADERS; do
    python ${H2DEF} --defsfilter=${filter} ${srcdir}/$h >> $output
done

sed -e "/of-object \"FsSession\"/ a \
      \  (unblock-threads t)" \
    -e "/of-object \"FsStream\"/ a \
      \  (unblock-threads t)" \
    -e "/of-object \"FsConference\"/ a \
      \  (unblock-threads t)" \
    -e "/define-method new_/ a \
      \  (caller-owns-return t)" \
    -i $output
