# R7000 Firmware Build Instructions

** Version: 1.0 **
** Date   : 2015/12/14 **

Note: 
      This package has been built successfully on 32-bit i386 Fedora 6 Linux 
      host machine. Compiling this package on platforms other than Fedora Core 6
      may have unexpected results.

## Install toolchain

1. You will need to use the  4.5.3 Linux 2.6.36 ARM Cross Compiler and Tools (arm-uclibc toolchain),
   which have also been posted to your account.

2. To install the toolcain
   1. The toolchain MUST be installed under /projects/hnd/tools/linux/ directory. After mkdir the dir,
            `tar xvfj hndtools-arm-linux-2.6.36-uclibc-4.5.3.tar.bz2`
   2. Add the path
            `export PATH=/projects/hnd/tools/linux/hndtools-arm-linux-2.6.36-uclibc-4.5.3/bin:$PATH`

## Build code

1. Put the file "R7000-V1.0.72_1.1.93_src.tar.zip" into a directory of your choice.
   Unzip and untar it with the following command:
        ```unzip R7000-V1.0.7.6_1.1.99_src.tar.zip
        tar xvf R7000-V1.0.7.6_1.1.99_src.tar```

2. Run the following commands to build image
        ```cd R7000-V1.0.7.6_1.1.99_src/src/router/
        make PROFILE=R7000 FW_TYPE=WW ARCH=arm PLT=arm LINUX_VERSION=2_6_36
	make install PROFILE=R7000 FW_TYPE=WW ARCH=arm PLT=arm LINUX_VERSION=2_6_36```

3. The final image is "R7000-V1.0.7.6_1.1.99_src/src/router/arm-uclibc/kernel_image.chk".
   User may upgrade to this image using the R7000 GUI "Router Upgrade" page.

