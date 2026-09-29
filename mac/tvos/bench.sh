#!/bin/sh
# usage: bench.sh <label> [extra xcodebuild settings...]  - builds Release, runs 75 s on the Apple TV, prints the perf log
D=${T2K_DEVICE:-30c9408192f798f4060a139edaf7e5dba848cc12}; L=$1; shift
cd "$(dirname "$0")"
xcodebuild -project Tempest2000TV.xcodeproj -scheme Tempest2000TV -configuration Release -destination "id=$D" -allowProvisioningUpdates -derivedDataPath build "$@" build 2>&1 | grep -E "error:|BUILD FAILED" 
xcrun devicectl device install app --device $D "build/Build/Products/Release-appletvos/Tempest 2000.app" >/dev/null 2>&1
echo "== $L"; timeout 75 xcrun devicectl device process launch --device $D --terminate-existing --console com.jnaina.tempest2000.tv 2>&1 | grep "T2K: perf" | sed 's/.*T2K: perf 600 frames | //'
