# Velvet Chords build
#   make            native build + all tests (x86_64/any Linux; for development)
#   make arm        32-bit ARM hard-float build for Gen1 MPC OS devices (needs arm-linux-gnueabihf-gcc)
#   make arm-test   run the plugin host test on the ARM build under qemu-arm (needs qemu-user)
#   make release    package dist/VelvetChords-<ver>.zip from the ARM build

VERSION := 1.0.0
SRC := src/vcmath.c src/chords.c src/layouts.c src/songs_gen.c src/presets_gen.c src/synth.c src/fx.c src/engine.c src/export.c src/midiport.c src/midiimport.c src/progression.c src/audioanalysis.c
WARN := -Wall -Wextra -Wno-unused-parameter
COMMON := -std=gnu99 -O2 $(WARN) -fPIC -fvisibility=hidden -fno-strict-aliasing

NATIVE_CC ?= gcc
ARM_CC ?= arm-linux-gnueabihf-gcc
ARM_FLAGS ?= -march=armv7-a -mfpu=neon -mfloat-abi=hard -mthumb
ARM_LDEXTRA ?= -static-libgcc

.PHONY: all native test arm arm-test release clean data skin

all: native test

data: src/songs_gen.c
src/songs_gen.c: data/songs.json tools/gen_songs.py
	python3 tools/gen_songs.py
src/presets_gen.c: data/presets.json tools/gen_presets.py
	python3 tools/gen_presets.py

build/native/velvetchords.so: $(SRC) src/plugin.c src/*.h | build/native
	$(NATIVE_CC) $(COMMON) -shared -o $@ $(SRC) src/plugin.c -Wl,-z,defs -ldl
build/native/test_engine: $(SRC) tests/test_engine.c src/*.h | build/native
	$(NATIVE_CC) -std=gnu99 -O1 -g $(WARN) -o $@ $(SRC) tests/test_engine.c -ldl -lm
build/native/alloc_probe.so: tests/alloc_probe.c | build/native
	$(NATIVE_CC) -std=gnu99 -O1 -shared -fPIC -o $@ tests/alloc_probe.c -ldl
build/native/test_host: tests/test_host.c src/vst2_abi.h | build/native
	$(NATIVE_CC) -std=gnu99 -O1 -g $(WARN) -o $@ tests/test_host.c src/chords.c -ldl -lm -lpthread
build/native/vc_export: $(SRC) tools/export_cli.c | build/native
	$(NATIVE_CC) -std=gnu99 -O2 $(WARN) -o $@ $(SRC) tools/export_cli.c -ldl -lm
build/native build/arm:
	mkdir -p $@

native: src/songs_gen.c build/native/velvetchords.so build/native/test_engine build/native/test_host build/native/alloc_probe.so build/native/vc_export

test: native
	./build/native/test_engine
	rm -rf build/native/exports && mkdir -p build/native/exports
	LD_PRELOAD=$(CURDIR)/build/native/alloc_probe.so VC_EXPORT_DIR=$(CURDIR)/build/native/exports ./build/native/test_host ./build/native/velvetchords.so build/native/demo
	mkdir -p build/native/fakealsa && $(NATIVE_CC) -shared -fPIC -o build/native/fakealsa/libasound.so.2 tests/fake_alsa.c
	LD_PRELOAD=$(CURDIR)/build/native/alloc_probe.so LD_LIBRARY_PATH=$(CURDIR)/build/native/fakealsa VC_FAKE_ALSA_LOG=$(CURDIR)/build/native/fake_alsa.log VC_EXPORT_DIR=$(CURDIR)/build/native/exports ./build/native/test_host ./build/native/velvetchords.so | tail -3
	rm -rf build/native/library && ./build/native/vc_export build/native/library && python3 tools/make_charts.py build/native/library/database.json build/native/library && cp build/native/library/SONG_CHARTS.md build/native/library/VERIFICATION_REPORT.md docs/
	python3 tests/check_midi.py build/native/library build/native/exports
	$(NATIVE_CC) -std=gnu99 -O1 -o build/native/list_params tools/list_params.c -ldl
	sh scripts/check_skin.sh

# regenerate the screen skin (needs python3-pil and the Poppins font); the result is committed in package/skin
skin: native
	$(NATIVE_CC) -std=gnu99 -O1 -o build/native/list_params tools/list_params.c -ldl
	rm -rf build/skin && python3 tools/make_skin.py build/native/velvetchords.so build/native/list_params build/skin
	rm -rf "package/skin/Velvet Chords - VST - Velvet Chords" && cp -r "build/skin/Velvet Chords - VST - Velvet Chords" package/skin/
	python3 tools/preview_skin.py "package/skin/Velvet Chords - VST - Velvet Chords" build/native/velvetchords.so build/native/list_params docs/skin-preview/preview "Play=1=0.103093" "Play=34=0.1875" "Play=114=0.5" "Edit=0=1" "Edit=34=0.6875" "Pattern=90=0.375" "Pattern=21=0.6667" "Pattern=92=0.3" "Pattern=15=1" "Tools=117=1" "Tools=118=0.333333" "Tools=119=0.5"

arm: src/songs_gen.c | build/arm
	$(ARM_CC) $(COMMON) $(ARM_FLAGS) -shared -o build/arm/velvetchords.so $(SRC) src/plugin.c $(ARM_LDEXTRA) -Wl,-z,defs -ldl -Wl,--hash-style=both -s
	$(ARM_CC) -std=gnu99 -O1 $(WARN) $(ARM_FLAGS) -o build/arm/test_host tests/test_host.c src/chords.c -ldl -lm -lpthread
	sh scripts/check_so.sh build/arm/velvetchords.so

arm-test: arm
	VC_EXPORT_DIR=/tmp/vc-arm-exports qemu-arm -L /usr/arm-linux-gnueabihf build/arm/test_host build/arm/velvetchords.so /tmp/vc-arm-demo

release: arm native
	sh scripts/release.sh $(VERSION)

clean:
	rm -rf build dist
