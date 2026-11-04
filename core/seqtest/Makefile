# Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
# Host tests for the glue logic (no Polyend code). Needs a desktop g++.   make test
CXX ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -O1 -I../source -I../../seqgen -I../interface/seqSession
SEQ = ../../seqgen/chordarp.cpp ../../seqgen/mono.cpp ../../seqgen/playhead.cpp
GLUE = ../source/seqTracksCore.cpp ../source/seqCut.cpp ../interface/seqSession/cutLayer.cpp
EXE := $(if $(filter Windows_NT,$(OS)),.exe,)

.PHONY: test clean preview
test: build/seq_glue_tests$(EXE) build/seq_perform_tests$(EXE) build/seq_mod_tests$(EXE) build/seq_play_tests$(EXE)
	build/seq_glue_tests$(EXE)
	build/seq_perform_tests$(EXE)
	build/seq_mod_tests$(EXE)
	build/seq_play_tests$(EXE)

build/seq_glue_tests$(EXE): test_main.cpp test_session.cpp test_cut.inc ../source/seqSessionLogic.h ../source/seqClipsLogic.h ../source/seqCut.h ../source/seqCutLogic.h ../interface/seqSession/cutLayer.h ../interface/seqSession/cutSessionLayer.h ../interface/seqSession/laneDraw.h ../interface/seqSession/laneView.h ../source/seqTracksCore.h ../source/seqTracksLogic.h ../source/seqTracksRec.h $(GLUE) $(SEQ)
	mkdir -p build
	$(CXX) $(CXXFLAGS) test_main.cpp test_session.cpp $(GLUE) $(SEQ) -o $@

build/seq_perform_tests$(EXE): test_perform.cpp ../source/seqPerformLogic.h ../source/seqModLogic.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) test_perform.cpp -o $@

build/seq_mod_tests$(EXE): test_mod.cpp ../source/seqModLogic.h ../source/seqSidecarLogic.h ../source/seqClipsLogic.h ../source/seqSongLogic.h ../source/seqTracksLogic.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) test_mod.cpp -o $@

# PNG previews of the CUT lane screen (plan 005 R4): draw ops from the host test, rasterised by lane_preview.py (needs Pillow)
preview: build/seq_glue_tests$(EXE)
	mkdir -p build/preview
	SEQ_LANE_PREVIEW=build/preview build/seq_glue_tests$(EXE) > /dev/null
	python lane_preview.py build/preview

build/seq_play_tests$(EXE): test_play.cpp ../source/seqPlayLogic.h ../source/seqTracksLogic.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) test_play.cpp -o $@

clean:
	rm -rf build
