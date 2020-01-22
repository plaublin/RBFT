#!/bin/sh
#
# Usage: ./make_libbyz.sh <cibles>

NB_CORES=$(($(grep cores /proc/cpuinfo | wc -l) + 1))

if [ $# -ne 1 ]; then
	LIST="verifier pir"
else
	LIST="$@"
fi

# compiles only libbyz (instead of install.sh which compiles everything)
for d in $LIST; do
	cd libbyz.$d
    echo "--> Compiling libbyz.$d ..."

    echo -n "make clean ..."
    make clean >/dev/null 2>&1
    if [ $? -ne 0 ] ; then
        echo "... make clean failed! Exiting"
        exit 1
    fi
    echo "... OK!"

    echo -n "make clobber ..."
    make clobber >/dev/null 2>&1
    if [ $? -ne 0 ] ; then
        echo "... make clobber failed! Exiting"
        exit 1
    fi
    echo "... OK!"


    echo -n "make ..."
    make -j$NB_CORES >/dev/null 2>&1
    if [ $? -ne 0 ] ; then
        echo "... make failed! Exiting"
        exit 1
    fi
    echo "... OK!"

	cd ..;
done

exit 0

