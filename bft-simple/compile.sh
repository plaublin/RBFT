#!/bin/bash

NB_CORES=$(($(grep cores /proc/cpuinfo | wc -l) + 1))


cd ..
./make_libbyz.sh

if [ $? -ne 0 ] ; then
    echo "Failed compilation of make_libbyz.sh, exiting here!"
    exit 1
fi

###############################################################

echo -n "--> Compiling the various RRBFT binaries ..."
cd bft-simple
make clean
make clobber
make -j$NB_CORES > output_make.log 2>&1
if [ $? -ne 0 ] ; then
    echo "Failed compilation of the RRBFT binaries, exiting here!"
    exit 1
fi
echo "... OK!"

exit 0
