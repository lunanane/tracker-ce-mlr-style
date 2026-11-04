# Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
# Host tests for the glue logic (no Polyend code). Needs a desktop g++.   make test
CXX ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -O1 -I../source -I../../seqgen -I../interface/seqSession
SEQ = ../../seqgen/chordarp.cpp ../../seqgen/mono.cpp ../../seqgen/playhead.cpp
GLUE = ../source/seqTracksCore.cpp ../source/seqCut.cpp ../interface/seqSession/cutLayer.cpp
EXE := $(if $(filter Windows_NT,$(OS)),.exe,)

.PHONY: test clean
test: build/seq_glue_tests$(EXE)
	build/seq_glue_tests$(EXE)

build/seq_glue_tests$(EXE): test_main.cpp test_cut.inc ../source/seqCut.h ../source/seqCutLogic.h ../interface/seqSession/cutLayer.h ../source/seqTracksCore.h ../source/seqTracksLogic.h ../source/seqTracksRec.h $(GLUE) $(SEQ)
	mkdir -p build
	$(CXX) $(CXXFLAGS) test_main.cpp $(GLUE) $(SEQ) -o $@

clean:
	rm -rf build
