#!/bin/sh
set -eu
ROOT="/src"
OUT=${OUT:-/out}
BUILD=/tmp/iop-toolchain
JOBS=${JOBS:-4}
export CC=${CC:-'gcc -m32 -static'}
export CFLAGS=${CFLAGS:-'-O2 -std=gnu89 -fcommon -fno-strict-aliasing'}
export LC_ALL=C TZ=UTC

mkdir -p "$BUILD/src"

tar -xzf "$ROOT/gcc_sources/gcc-2.8.1.tar.gz" -C "$BUILD/src"
tar -xzf "$ROOT/gcc_sources/binutils-2.9.1.tar.gz" -C "$BUILD/src"

cd "$BUILD/src"
patch --batch -p0 < "$ROOT/iop-gcc.patch"
patch --batch -p1 < "$ROOT/iop-fixes.patch"
# Make sure that make doesn't try to re-run configure.
touch "$BUILD/src/gcc-2.8.1/configure"

mkdir -p "$BUILD/binutils" "$BUILD/gcc" "$BUILD/install/bin"

cd "$BUILD/binutils"

echo 'Configuring binutils'
"$BUILD/src/binutils-2.9.1/configure" --host=i686-pc-linux-gnu --target=mipsel-scei-elfl \
    --disable-nls --disable-shared --prefix="$BUILD/install"

echo 'Building binutils'
make -j"$JOBS" all-binutils all-gas all-ld CFLAGS="$CFLAGS" LDFLAGS="${BINUTILS_LDFLAGS:-}" MAKEINFO=true

make install-binutils install-gas install-ld LDFLAGS="${BINUTILS_LDFLAGS:-}" MAKEINFO=true

cd "$BUILD/gcc"

echo 'Configuring GCC'
"$BUILD/src/gcc-2.8.1/configure" --host=i686-pc-linux-gnu --target=mipsel-scei-elfl \
    --with-gnu-as --with-gnu-ld --prefix="$BUILD/install"

echo 'Building GCC'
make -j"$JOBS" cc1 cpp CFLAGS="$CFLAGS" MAKEINFO=true

mkdir -p "$OUT/iop"
cp cc1 cpp "$OUT/iop/"
for tool in as ld ar nm objcopy objdump; do
    cp -L "$BUILD/install/bin/mipsel-scei-elfl-$tool" "$OUT/iop/iop-$tool"
done
