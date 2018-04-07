#!/bin/bash


/home/jason/clang-trunk/bin/clang++ --target=arm-linux $1 -O3 -Wall -Wextra -c -o $1.o -stdlib=libc++ -std=c++2a -march=armv4
arm-objcopy -O binary $1.o -j .text $1.arm
