# Toolchains

This folder has toolchains needed to build C/C++ code for the PlayStation 2's EE (main CPU) and IOP.

The compilers are built from source in a 32-bit x86 Alpine Linux container and statically linked against MUSL libc for the best compatibility. Building them is done by running `build_toolchain.sh`.

We provide a few patches to get these older compilers working on modern systems. None of these patches modify the behavior of the compiler.

# Original GCC Sources

The files in `gcc_sources` were downloaded from:

https://ftp.gnu.org/gnu/gcc/gcc-2.8.1.tar.gz
https://ftp.gnu.org/gnu/binutils/binutils-2.9.1.tar.gz
