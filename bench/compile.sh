CROSS_COMPILE=/segfs/linux/dance_sdk/toolchain/arm-buildroot-linux-uclibcgnueabi/bin/arm-buildroot-linux-uclibcgnueabi-

MICROPY_INSTALL_DIR=../dance/install_dir
MICROPY_INC_DIR=$MICROPY_INSTALL_DIR/include
MICROPY_LIB_DIR=$MICROPY_INSTALL_DIR/lib

$CROSS_COMPILE\gcc \
-Wall -O2 -std=c99 \
-O2 -DNDEBUG -fno-crossjumping \
-Wpointer-arith -Wuninitialized -ansi -std=gnu99 \
-DMP_CONFIGFILE="<mpconfigport_fast.h>" \
-DUNIX \
-DMICROPY_PY_TIME=1 \
-MD \
-static \
-I$MICROPY_INC_DIR \
main.c \
-L$MICROPY_LIB_DIR -lmicropython \
-L$MICROPY_LIB_DIR -lffi \
-lpthread -lm -ldl -lc

$CROSS_COMPILE\strip ./a.out
