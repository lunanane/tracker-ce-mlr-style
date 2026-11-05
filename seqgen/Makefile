# Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
# seqgen host tests. Needs only a desktop g++ (and make). Run from this directory.
#   make test         build + run the unit tests
#   make lua-compare  differential test against the c31 Lua pages (needs Python with `lupa`)
#   make hr-compare   differential test of the ported HYPER_RASP scripts against the real Lua (HR_SCRIPTS=dir, needs lupa)
#   make arm          compile the library for Cortex-M4F and print object sizes (needs arm-none-eabi-g++)
CXX ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -Wdouble-promotion -O1
SRC = chordarp.cpp mono.cpp playhead.cpp hr.cpp hr_stab.cpp hr_arpseq.cpp hr_chordarp.cpp hr_bass.cpp hr_euclid.cpp hr_gridseq.cpp hr_stepfollow.cpp
PYTHON ?= python
EXE := $(if $(filter Windows_NT,$(OS)),.exe,)
LUA ?=   # path to monome_iii_c34.lua of the grid-composer (or monome_iii_c31.lua for the older script; the port follows c34, so it will differ); required for lua-compare
FUZZ ?= 40

HR_SCRIPTS ?= E:/claude/_HYPER_RASP/scripts   # HYPER_RASP scripts directory (hr-compare)

.PHONY: test lua-compare hr-compare arm clean
test: build/seqgen_tests$(EXE)
	build/seqgen_tests$(EXE)

build/seqgen_tests$(EXE): test/test_main.cpp test/test_cases2.inc test/test_cases3.inc test/test_pads.inc test/test_playhead.inc test/test_hr.inc $(SRC) seqgen.h chordarp.h mono.h playhead.h hr.h hr_scripts.h hr_stab.h hr_arpseq.h hr_chordarp.h hr_bass.h hr_euclid.h hr_gridseq.h hr_stepfollow.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) test/test_main.cpp $(SRC) -o $@

build/lua_driver$(EXE): test/lua_driver.cpp $(SRC) seqgen.h chordarp.h mono.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) test/lua_driver.cpp $(SRC) -o $@

lua-compare: build/lua_driver$(EXE)
	$(if $(LUA),,$(error set LUA=path/to/monome_iii_c34.lua))$(PYTHON) -I test/lua_compare.py --lua $(LUA) --driver build/lua_driver$(EXE) --fuzz $(FUZZ) --seed 1 --script test/scripts/*.txt

build/hr_driver$(EXE): test/hr_driver.cpp $(SRC) seqgen.h hr.h hr_scripts.h hr_stab.h hr_arpseq.h hr_chordarp.h hr_bass.h hr_euclid.h hr_gridseq.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) test/hr_driver.cpp $(SRC) -o $@

hr-compare: build/hr_driver$(EXE)
	$(PYTHON) -I test/hr_compare.py --scripts-dir $(HR_SCRIPTS) --driver build/hr_driver$(EXE) --fuzz $(FUZZ)

ARMFLAGS = -std=gnu++17 -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -Os -fno-exceptions -fno-rtti \
           -Wall -Wextra -Wdouble-promotion -ffunction-sections -fdata-sections
arm:
	mkdir -p build
	arm-none-eabi-g++ $(ARMFLAGS) -c chordarp.cpp -o build/chordarp.o
	arm-none-eabi-g++ $(ARMFLAGS) -c mono.cpp -o build/mono.o
	arm-none-eabi-g++ $(ARMFLAGS) -c playhead.cpp -o build/playhead.o
	arm-none-eabi-size build/chordarp.o build/mono.o build/playhead.o

clean:
	rm -rf build
