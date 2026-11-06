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

BUILD=/tmp/ee-toolchain
mkdir -p "$BUILD"
tar -xzf "$ROOT/gcc_sources/ee-2.9-991111.tar.gz" -C "$BUILD"
cd "$BUILD"
patch --batch -p0 < "$ROOT/ee-gcc.patch"
cd "$BUILD/src"
patch --batch -p1 < "$ROOT/ee-fixes.patch"

export BISON_SIMPLE="$BUILD/src/bison/bison.simple"
export BISON_HAIRY="$BUILD/src/bison/bison.hairy"

for component in intl libiberty bison bfd opcodes gas binutils ld; do
    mkdir -p "$BUILD/build/$component"
    cd "$BUILD/build/$component"
    echo "Building EE $component"
    "$BUILD/src/$component/configure" --host=i686-pc-linux-gnu --target=ee \
        --disable-nls --disable-shared --prefix="$BUILD/install"
    link_flags=${BINUTILS_LDFLAGS:-}
    if [ "$component" = bison ]; then
        link_flags=
    fi
    make -j"$JOBS" CFLAGS="$CFLAGS" LDFLAGS="$link_flags" \
        YACC="$BUILD/build/bison/bison -y" MAKEINFO=true
done

mkdir -p "$BUILD/build/gcc"
cd "$BUILD/build/gcc"
echo 'Building EE GCC'
AS="$BUILD/build/gas/as-new" "$BUILD/src/gcc/configure" \
    --host=i686-pc-linux-gnu --target=ee --with-gnu-as --with-gnu-ld \
    --enable-c-cpplib --enable-languages=c,c++ --prefix="$BUILD/install"
make -j"$JOBS" cc1 cc1plus cpp xgcc collect2 CFLAGS="$CFLAGS" \
    BISON="$BUILD/build/bison/bison" MAKEINFO=true

mkdir -p "$OUT/ee"
cp cc1 cc1plus cpp collect2 "$OUT/ee/"
cp xgcc "$OUT/ee/ee-gcc"
cp "$BUILD/build/gas/as-new" "$OUT/ee/as"
cp "$BUILD/build/ld/ld-new" "$OUT/ee/ld"
cp "$BUILD/build/binutils/nm-new" "$OUT/ee/nm"
for tool in ar objcopy objdump; do
    cp "$BUILD/build/binutils/$tool" "$OUT/ee/ee-$tool"
done
