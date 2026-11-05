# 32-bit x86 build with alpine and statically linked MUSL.
FROM --platform=linux/386 docker.io/library/alpine:3.22@sha256:2e0b81d60597f449baa56dc25d355367869d2dca4cd57e866f1e2b52af3eb55f
RUN apk add --no-cache gcc musl-dev make patch flex bison binutils
WORKDIR /src
COPY gcc_sources gcc_sources
COPY iop-gcc.patch iop-gcc.patch
COPY iop-fixes.patch iop-fixes.patch
COPY ee-gcc.patch ee-gcc.patch
COPY ee-fixes.patch ee-fixes.patch
COPY build.sh build.sh
ENV OUT=/out CC="gcc -m32 -static" BINUTILS_LDFLAGS="-all-static" CFLAGS="-O2 -std=gnu89 -fcommon -fno-strict-aliasing -fpermissive"
ENTRYPOINT ["/bin/sh", "/src/build.sh"]
