#!/bin/sh
# Emulation regression check.  Runs t2k_host headless through several scripted play-throughs and compares the hash of
# every video frame, every audio sample and the full emulator state (every 100 frames) with values recorded from the
# unmodified core.  Any change to the core (blitter, DSP, ...) that alters emulation, even by one pixel, shows up here.
#   ./regress.sh            (run `make t2k_host` first)
cd "$(dirname "$0")"; fail=0
check() { want=$1; name=$2; shift 2
  got=$(./t2k_host "$@" 2>&1 | grep "test ok" | sed 's/.*video/video/')
  if [ "$got" = "$want" ]; then echo "ok    $name"; else echo "FAIL  $name"; echo "   want $want"; echo "   got  $got"; fail=1; fi; }
check "video d7bbe0638c748355 audio c90c6712e93c555e state 576d8f31a2e6580d" "attract, 3000 frames" --test-frames 3000
check "video c581fce4af617ab3 audio 6a2fa6a224f607c5 state 9f4d0a1842d538b0" "hold fire+jump, 6000" --test-frames 6000 --press 300:310:0 --press 500:510:0 --press 700:710:0 --press 900:6000:0 --press 900:6000:6
check "video 97b55f6262168871 audio abef27bac45d2964 state ec14a413ef05941e" "left/right sweeps, 8000" --test-frames 8000 --press 300:310:0 --press 500:510:0 --press 700:710:0 --press 900:8000:0 --press 1000:1300:2 --press 1500:1900:3 --press 2400:2900:2 --press 3300:3700:3 --press 4500:5100:2 --press 5500:6200:3 --press 3000:3100:5 --press 6500:6600:5
check "video e92f6f663488fb75 audio 7fc6d7320824ad1c state a9fd9e7e01451f0b" "option/pause, 5000" --test-frames 5000 --press 300:310:3 --press 400:410:0 --press 600:610:0 --press 800:5000:0 --press 1500:1510:2 --press 1600:1610:4
exit $fail
