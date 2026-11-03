# seqgen host tests. Needs only a desktop g++ (and make). Run from this directory.
#   make test         build + run the unit tests
#   make lua-compare  differential test against the c31 Lua pages (needs Python with `lupa`)
#   make arm          compile the library for Cortex-M4F and print object sizes (needs arm-none-eabi-g++)
CXX ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -Wdouble-promotion -O1
SRC = chordarp.cpp mono.cpp
PYTHON ?= python
EXE := $(if $(filter Windows_NT,$(OS)),.exe,)
C31 ?= D:/OneDrive/CodeLibraries/iii/monome_iii_c31.lua
FUZZ ?= 40

.PHONY: test lua-compare arm clean
test: build/seqgen_tests$(EXE)
	build/seqgen_tests$(EXE)

build/seqgen_tests$(EXE): test/test_main.cpp test/test_cases2.inc $(SRC) seqgen.h chordarp.h mono.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) test/test_main.cpp $(SRC) -o $@

build/lua_driver$(EXE): test/lua_driver.cpp $(SRC) seqgen.h chordarp.h mono.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) test/lua_driver.cpp $(SRC) -o $@

lua-compare: build/lua_driver$(EXE)
	$(PYTHON) -I test/lua_compare.py --c31 $(C31) --driver build/lua_driver$(EXE) --fuzz $(FUZZ) --seed 1 --script test/scripts/*.txt

ARMFLAGS = -std=gnu++17 -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -Os -fno-exceptions -fno-rtti \
           -Wall -Wextra -Wdouble-promotion -ffunction-sections -fdata-sections
arm:
	mkdir -p build
	arm-none-eabi-g++ $(ARMFLAGS) -c chordarp.cpp -o build/chordarp.o
	arm-none-eabi-g++ $(ARMFLAGS) -c mono.cpp -o build/mono.o
	arm-none-eabi-size build/chordarp.o build/mono.o

clean:
	rm -rf build
