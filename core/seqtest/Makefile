# Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
# Host tests for the glue logic (no Polyend code). Needs a desktop g++.   make test
CXX ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -O1 -I../source -I../../seqgen -I../interface/seqSession -I../libs/Audio/Polyverb
SEQ = ../../seqgen/chordarp.cpp ../../seqgen/mono.cpp ../../seqgen/playhead.cpp ../../seqgen/hr.cpp ../../seqgen/hr_stab.cpp ../../seqgen/hr_arpseq.cpp ../../seqgen/hr_chordarp.cpp ../../seqgen/hr_bass.cpp ../../seqgen/hr_euclid.cpp ../../seqgen/hr_gridseq.cpp ../../seqgen/hr_stepfollow.cpp
HRSRC = ../../seqgen/hr.cpp ../../seqgen/hr_stab.cpp ../../seqgen/hr_arpseq.cpp ../../seqgen/hr_chordarp.cpp ../../seqgen/hr_bass.cpp ../../seqgen/hr_euclid.cpp ../../seqgen/hr_gridseq.cpp ../../seqgen/hr_stepfollow.cpp
GLUE = ../source/seqTracksCore.cpp ../source/seqCut.cpp ../interface/seqSession/cutLayer.cpp
EXE := $(if $(filter Windows_NT,$(OS)),.exe,)

.PHONY: test clean preview
test: build/seq_cpu_tests$(EXE) build/seq_glue_tests$(EXE) build/seq_perform_tests$(EXE) build/seq_mod_tests$(EXE) build/seq_play_tests$(EXE) build/seq_fx_tests$(EXE)
	build/seq_cpu_tests$(EXE)
	build/seq_glue_tests$(EXE)
	build/seq_perform_tests$(EXE)
	build/seq_mod_tests$(EXE)
	build/seq_play_tests$(EXE)
	build/seq_fx_tests$(EXE)

build/seq_glue_tests$(EXE): test_main.cpp test_session.cpp test_cut.inc ../source/seqSessionLogic.h ../source/seqClipsLogic.h ../source/seqCut.h ../source/seqCutLogic.h ../interface/seqSession/cutLayer.h ../interface/seqSession/cutSessionLayer.h ../interface/seqSession/laneDraw.h ../interface/seqSession/laneView.h ../source/seqTracksCore.h ../source/seqTracksLogic.h ../source/seqTracksRec.h ../source/seqRecLogic.h test_rec.inc test_hr.inc $(GLUE) $(SEQ)
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

build/seq_play_tests$(EXE): test_play.cpp ../source/seqPlayLogic.h ../source/seqTracksLogic.h $(HRSRC)
	mkdir -p build
	$(CXX) $(CXXFLAGS) test_play.cpp $(HRSRC) -o $@

build/seq_fx_tests$(EXE): test_fx.cpp ../source/seqFxDsp.h ../source/seqFxLogic.h ../source/seqSidecarLogic.h ../libs/Audio/Polyverb/ring_reverb.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) test_fx.cpp -o $@

clean:
	rm -rf build

# plan 009 P (CPU savings): bit-exact / bounded host tests against frozen copies of the pre-change DSP (seqtest/ref)
CPUREF = $(filter-out -I../libs/Audio/Polyverb,$(CXXFLAGS)) -Iref/polyverb -std=gnu++11
DELAYINC = -Istubs -I../libs/Audio -I../libs/Audio/utility -I../config -I../include -I../libs/Audio/Polyverb -DKINETISL -DAudio_h_ -include stubs/Audio.h -include stubs/dspinst.h
build/seq_cpu_tests$(EXE): test_cpu.cpp cpu_delay_tests.inc cpu_reverb_wrap.inc cpu_reverb_ref.cpp cpu_reverb_new.cpp ../libs/Audio/Polyverb/reverb.h ../libs/Audio/Polyverb/fx_engine.h ref/polyverb/reverb.h ref/polyverb/fx_engine.h ../libs/Audio/effect_shortDelay.cpp ../libs/Audio/effect_shortDelay.h ref/delay/ref_shortDelay.cpp ref/delay/ref_shortDelay.h stubs/audio_stream_stub.cpp ../libs/Audio/effect_limiter.cpp ../libs/Audio/effect_limiter.h ref/limiter/ref_limiter.cpp ref/limiter/ref_limiter.h ../libs/Audio/filter_variable.cpp ../libs/Audio/amplifier.cpp ../libs/Audio/monoToStereo.cpp ../libs/Audio/mixer.cpp ../libs/Audio/dspApplyGain.cpp stubs/dspinst.h
	mkdir -p build
	$(CXX) $(CPUREF) -c cpu_reverb_ref.cpp -o build/cpu_reverb_ref.o
	$(CXX) $(CXXFLAGS) -std=gnu++11 -c cpu_reverb_new.cpp -o build/cpu_reverb_new.o
	$(CXX) $(DELAYINC) $(CXXFLAGS) -std=gnu++11 -c ../libs/Audio/effect_shortDelay.cpp -o build/effect_shortDelay.o
	$(CXX) $(DELAYINC) $(CXXFLAGS) -std=gnu++11 -Iref/delay -c ref/delay/ref_shortDelay.cpp -o build/ref_shortDelay.o
	$(CXX) $(DELAYINC) $(CXXFLAGS) -std=gnu++11 -c stubs/audio_stream_stub.cpp -o build/audio_stream_stub.o
	$(CXX) $(DELAYINC) -I../AudioEngine/ConstantValues $(CXXFLAGS) -std=gnu++11 -c ../libs/Audio/effect_limiter.cpp -o build/effect_limiter.o
	for f in filter_variable amplifier monoToStereo mixer dspApplyGain; do $(CXX) $(DELAYINC) $(CXXFLAGS) -std=gnu++11 -c ../libs/Audio/$$f.cpp -o build/$$f.o || exit 1; done
	for f in ref_amplifier ref_monoToStereo; do $(CXX) $(DELAYINC) -Iref/voice $(CXXFLAGS) -std=gnu++11 -c ref/voice/$$f.cpp -o build/$$f.o || exit 1; done
	$(CXX) $(DELAYINC) -I../AudioEngine/ConstantValues -Iref/limiter $(CXXFLAGS) -std=gnu++11 -c ref/limiter/ref_limiter.cpp -o build/ref_limiter.o
	$(CXX) $(DELAYINC) $(CXXFLAGS) -std=gnu++11 -Iref/delay -Iref/limiter -Iref/voice test_cpu.cpp build/cpu_reverb_ref.o build/cpu_reverb_new.o build/effect_shortDelay.o build/ref_shortDelay.o build/audio_stream_stub.o build/effect_limiter.o build/ref_limiter.o build/filter_variable.o build/amplifier.o build/monoToStereo.o build/mixer.o build/dspApplyGain.o build/ref_amplifier.o build/ref_monoToStereo.o -o $@
