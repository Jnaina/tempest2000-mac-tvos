# Tempest 2000 for macOS
#
#   brew install sdl2          (once)
#   cd mac && make             builds everything and creates ./t2000.abs
#   make run                   play
#   make single                one self-contained executable ./t2k (core + game embedded)
#   make app                   build T2K.app (self-contained if dylibbundler is installed)
#
# What "make" does:
#   1. builds the rmac assembler and rln linker (from ../rmac and ../rln, cloned if missing)
#   2. assembles the Tempest 2000 sources (../src) into t2000.abs and verifies it is
#      byte-for-byte the original 1994 build (MD5 check)
#   3. builds the Virtual Jaguar emulation core (third_party/) as a shared library
#   4. builds t2k_host, the SDL2 front end that runs t2000.abs on that core

ROOT     := $(abspath ..)
CORE_DIR := third_party/virtualjaguar-libretro
CORE_URL := https://github.com/libretro/virtualjaguar-libretro.git
CORE_SHA := dd332ea34ef62106af599b7d9267571843873c8e
NPROC    := $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 2)
MD5      := $(shell command -v md5sum >/dev/null 2>&1 && echo md5sum || echo "md5 -r")
UNAME    := $(shell uname -s)
ifeq ($(UNAME),Darwin)
  CORE_LIB := virtualjaguar_libretro.dylib
else
  CORE_LIB := virtualjaguar_libretro.so
endif

# The Command Line Tools SDK can be newer than the installed linker (its .tbd files fail to
# parse: "ld: tapi error: malformed file"). Prefer the full Xcode SDK when present.
ifeq ($(shell uname -s),Darwin)
  ifndef SDKROOT
    XCODE_SDK := /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
    ifneq ($(wildcard $(XCODE_SDK)),)
      export SDKROOT := $(XCODE_SDK)
    endif
  endif
endif

CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=gnu11 -Wall -Wextra -Wno-unused-parameter $(shell sdl2-config --cflags)
LDLIBS  += $(shell sdl2-config --libs) -ldl -lm

all: t2k_host $(CORE_LIB) t2000.abs

# ---------------------------------------------------------------- toolchain
$(ROOT)/rmac/rmac:
	@[ -d $(ROOT)/rmac ] || git clone --depth 1 https://github.com/mwenge/rmac.git $(ROOT)/rmac
	$(MAKE) -C $(ROOT)/rmac

$(ROOT)/rln/rln:
	@[ -d $(ROOT)/rln ] || git clone --depth 1 http://tiddly.mooo.com:5000/rln/rln.git $(ROOT)/rln
	$(MAKE) -C $(ROOT)/rln

# ------------------------------------------------ the game, from the sources
SRC_FILES := $(wildcard $(ROOT)/src/*.s $(ROOT)/src/*.inc $(ROOT)/src/gpu/*.gas)
GPU_MODULES := donky camel antelope goat llama horse ox stoat xcamel

t2000.abs: $(SRC_FILES) $(ROOT)/rmac/rmac $(ROOT)/rln/rln
	mkdir -p $(ROOT)/src/bin
	cd $(ROOT) && for m in $(GPU_MODULES); do \
	    ./rmac/rmac -fr -mtom -l -isrc src/gpu/$$m.gas -o src/bin/$$m.o || exit 1; done
	cd $(ROOT) && ./rmac/rmac -fb -isrc -l src/yak.s -o src/bin/yak.cof
	cd $(ROOT) && ./rmac/rmac -fb -isrc src/yakgpu.s -o src/bin/yakgpu.cof
	cd $(ROOT) && ./rmac/rmac -fb -isrc src/vidinit.s -o src/bin/vidinit.cof
	cd $(ROOT) && ./rmac/rmac -fb -Isrc src/images_sounds.s -o src/bin/images_sounds.o
	cd $(ROOT) && ./rln/rln -z -e -a 802000 4000 efa8 src/bin/yak.cof src/bin/vidinit.cof \
	    src/bin/yakgpu.cof src/bin/images_sounds.o -o t2000.abs
	cp $(ROOT)/t2000.abs t2000.abs
	@echo "44e71799ee06615a59ff57b2c8a1ef52  t2000.abs" > .t2000.md5
	@$(MD5) t2000.abs | sed 's/ .*//' | tr -d '\n' > .t2000.got
	@if [ "$$(cat .t2000.got)" = "44e71799ee06615a59ff57b2c8a1ef52" ]; then \
	    echo "t2000.abs: byte-identical to the original 1994 build"; \
	 else echo "WARNING: t2000.abs differs from the reference build ($$(cat .t2000.got))"; fi
	@rm -f .t2000.md5 .t2000.got

# ------------------------------------------------------------ emulator core
$(CORE_DIR)/Makefile:
	git init -q $(CORE_DIR)
	cd $(CORE_DIR) && git remote add origin $(CORE_URL) && git fetch -q --depth 1 origin $(CORE_SHA) \
	    && git checkout -q FETCH_HEAD

$(CORE_LIB): $(CORE_DIR)/Makefile
	$(MAKE) -C $(CORE_DIR) -j$(NPROC)
	cp $(CORE_DIR)/$(CORE_LIB) $(CORE_LIB)

# --------------------------------------------------------------------- host
t2k_host: t2k_host.c libretro.h
	$(CC) $(CFLAGS) -o $@ t2k_host.c $(LDLIBS)

run: all
	./t2k_host

# ---------------------------------------------------------- single binary
# One self-contained executable: emulator core linked in, t2000.abs embedded.
#   make single   ->   ./t2k
CORE_OBJS = $(shell find $(CORE_DIR) -name '*.o' 2>/dev/null)
SDL_A     := $(shell sdl2-config --prefix)/lib/libSDL2.a
ifneq ($(wildcard $(SDL_A)),)
  SDL_LINK := $(subst -lSDL2,$(SDL_A),$(shell sdl2-config --static-libs))
else
  SDL_LINK := $(shell sdl2-config --libs)
endif

t2000_abs.h: t2000.abs
	python3 bin2c.py t2000.abs t2000_abs.h t2000_abs

# (falls back to the shared SDL2 library if static linking of SDL2 is not possible)
t2k: t2k_host.c libretro.h t2000_abs.h $(CORE_LIB)
	$(CC) $(CFLAGS) -DT2K_STATIC -o $@ t2k_host.c $(shell find $(CORE_DIR) -name '*.o') $(SDL_LINK) -lm -lpthread \
	 || $(CC) $(CFLAGS) -DT2K_STATIC -o $@ t2k_host.c $(shell find $(CORE_DIR) -name '*.o') $(shell sdl2-config --libs) -lm -lpthread

single: t2k
	@echo "built ./t2k - a single executable (needs no other files)"

# ------------------------------------------------------------------ bundle
app: all
	rm -rf T2K.app
	mkdir -p T2K.app/Contents/MacOS T2K.app/Contents/Resources T2K.app/Contents/Frameworks
	cp t2k_host T2K.app/Contents/MacOS/T2K
	cp $(CORE_LIB) t2000.abs T2K.app/Contents/Resources/
	cp Info.plist T2K.app/Contents/Info.plist
	@if command -v dylibbundler >/dev/null; then \
	    dylibbundler -od -b -x T2K.app/Contents/MacOS/T2K -d T2K.app/Contents/Frameworks \
	        -p @executable_path/../Frameworks/ ; \
	 else echo "note: 'brew install dylibbundler' makes the .app independent of Homebrew's SDL2"; fi

clean:
	rm -f t2k_host t2k t2000.abs t2000_abs.h $(CORE_LIB)
	rm -rf T2K.app
	-$(MAKE) -C $(CORE_DIR) clean

.PHONY: all run app single clean
