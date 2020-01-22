# Am I root and is there sudo on this machine?
if [ ! $(whoami) == "root" ] && [ $(which sudo) ]; then
   SUDO=sudo
else
   SUDO=""
fi

$SUDO apt-get install g++ automake flex libtool libgmp3c2 libgmp3-dev bison ccache ia32-libs libc6-dev-i386 lib32gmp3-dev libgmp3-dev g++-multilib &&

NB_CORES=$(($(grep cores /proc/cpuinfo | wc -l) + 1))

#Compiling sfslite
rm -rf sfs  
mkdir sfs  
export BFTHOME=`pwd`  
cd sfslite  
aclocal  
autoconf  
autoconf  
libtoolize --force --copy 
sh -x setup.gnu -f -i -s
libtoolize --force --copy 
sh -x setup.gnu -f -i -s 
export CPPFLAGS="-m32 -L/usr/lib32" 
export CFLAGS="${CPPFLAGS}" 
export CXXFLAGS="${CPPFLAGS}" 
export AM_CFLAGS="${CPPFLAGS}" 
export AM_CPPFLAGS="${CPPFLAGS}" 
export AM_CXXFLAGS="${CPPFLAGS}" 
export LDFLAGS="${CPPFLAGS}" 
./configure --prefix=$BFTHOME/sfs
make -j$NB_CORES  
make install  

exit


cd ..  
#rm -f sfs ; ln -s sfslite-1.2/install sfs  
rm -f gmp ; ln -s /usr/lib32 gmp  
./make_libbyz.sh  

cd bft-simple  
make -j$NB_CORES

