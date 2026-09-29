#!/bin/sh
# Builds the Virtual Jaguar core as static libraries for tvOS.
#   ./build_core.sh                       device + simulator, tuned for Apple TV 4K (A10X and newer)
#   MCPU=apple-a15 LTO=1 OUT=x ./build_core.sh dev    just the device lib, as libvjcore_x.a
# Works on a scratch copy so the macOS objects in ../third_party are left alone.
set -e
cd "$(dirname "$0")"
SRC=../third_party/virtualjaguar-libretro
DEV=$(xcrun --sdk appletvos --show-sdk-path); SIM=$(xcrun --sdk appletvsimulator --show-sdk-path)
MCPU=${MCPU:-apple-a10}; EXTRA="-mcpu=$MCPU -mtune=$MCPU"; [ "$LTO" = 1 ] && EXTRA="$EXTRA -flto"
export SDKROOT=
for v in ${@:-dev sim}; do
  W=build_$v; rm -rf $W; mkdir -p $W
  rsync -a --exclude '*.o' --exclude '*.dylib' --exclude .git "$SRC/" $W/
  if [ $v = dev ]; then SDK=$DEV; MIN=-mappletvos-version-min=15.0; else SDK=$SIM; MIN=-mappletvsimulator-version-min=15.0; fi
  make -C $W -j$(sysctl -n hw.ncpu) platform=tvos-arm64 IOSSDK="$SDK" MINVERSION="$MIN $EXTRA" >/dev/null 2>&1
  # the core plus the libretro-common helpers it needs, as one static archive
  O=libvjcore_${OUT:-$v}.a; rm -f $O; find $W -name '*.o' | xargs ar rcs $O 2>/dev/null
  rm -rf $W; ls -la $O
done
