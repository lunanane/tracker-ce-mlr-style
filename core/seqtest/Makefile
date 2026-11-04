# Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
# Host tests for the glue logic (no Polyend code). Needs a desktop g++.   make test
CXX ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -O1 -I../source -I../../seqgen
SEQ = ../../seqgen/chordarp.cpp ../../seqgen/mono.cpp
EXE := $(if $(filter Windows_NT,$(OS)),.exe,)

.PHONY: test clean
test: build/seq_glue_tests$(EXE)
	build/seq_glue_tests$(EXE)

build/seq_glue_tests$(EXE): test_main.cpp test_session.cpp ../source/seqSessionLogic.h ../source/seqTracksCore.cpp ../source/seqTracksCore.h ../source/seqTracksLogic.h ../source/seqTracksRec.h $(SEQ)
	mkdir -p build
	$(CXX) $(CXXFLAGS) test_main.cpp test_session.cpp ../source/seqTracksCore.cpp $(SEQ) -o $@

clean:
	rm -rf build
