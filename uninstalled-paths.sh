#!/bin/sh

# Generate environment for using Farsight 2 from Git checkout.

realpath_ ()
{
    ( cd $1 2>/dev/null; pwd )
}

pjoin ()
{
    a=$(realpath_ $1)
    b="$2"

    [ -z "$a" ] && { echo $b ; return ; }

    if [ -z "$b" ]; then
        # Existing path list is empty.
        echo "$a"
    elif ! echo "$b" | sed -e 's/:/\\n/g' | grep -q "^$a$"; then
        # New path is not in path list.
        echo "$a:$b"
    else
        # New path is already in path list.
        echo "$b"
    fi
}

p=$(realpath_ $(dirname $0))
ppath="$FS_PLUGIN_PATH"

echo export PYTHONPATH=`pjoin "$p/python/.libs" "$PYTHONPATH"`
echo export GST_PLUGIN_PATH=`pjoin "$p/gst" "$GST_PLUGIN_PATH"`

for i in `find $p/transmitters -maxdepth 1 -type d -exec basename '{}' ';'`; do
    ppath=`pjoin "$p/transmitters/$i/.libs" "$ppath"`
done

echo "export FS_PLUGIN_PATH=$ppath"
