#!/usr/bin/env bash


export FFI_DIR=$HOME/segfs/repo/libffi/libffi-3.2.1/arm-unknown-none
export MICROPY_DIR=/buffer/PCLAB2451/lementec/repo/micropython
export CROSS_COMPILE=/segfs/linux/dance_sdk/toolchain/arm-buildroot-linux-uclibcgnueabi/bin/arm-buildroot-linux-uclibcgnueabi-

#
# apply patches

THIS_DIR=$PWD
PATCH_DIR=$THIS_DIR/../patch
cd $MICROPY_DIR
for p in $PATCH_DIR/*.patch; do < $p patch -p1; done
cd $THIS_DIR


#
# build libmicropython and headers

PKG_CONFIG_PATH=$FFI_DIR \
CROSS_COMPILE=$CROSS_COMPILE \
make clean

rm -rf ./build

PKG_CONFIG_PATH=$FFI_DIR \
CROSS_COMPILE=$CROSS_COMPILE \
make lib-fast


#
# install libmicropython and headers

rm -rf install_dir

mkdir install_dir
mkdir install_dir/include
mkdir install_dir/include/py
mkdir install_dir/include/genhdr
cp *.h install_dir/include
find $MICROPY_DIR/py/ -name \*.h -exec cp {} install_dir/include/py \;
find ./build/genhdr -name \*.h -exec cp {} install_dir/include/genhdr \;

mkdir install_dir/lib
cp libmicropython.a install_dir/lib


#
# install libffi

if [ -e $FFI_DIR/../install/lib/libffi.a ]; then
    cp $FFI_DIR/../install/lib/libffi.a install_dir/lib
fi
