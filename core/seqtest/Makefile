# Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
# Host tests for the glue logic (no Polyend code). Needs a desktop g++.   make test
CXX ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -O1 -I../source -I../../seqgen -I../interface/seqSession -I../libs/Audio/Polyverb
SEQ = ../../seqgen/chordarp.cpp ../../seqgen/mono.cpp ../../seqgen/playhead.cpp ../../seqgen/hr.cpp ../../seqgen/hr_stab.cpp ../../seqgen/hr_arpseq.cpp ../../seqgen/hr_chordarp.cpp ../../seqgen/hr_bass.cpp ../../seqgen/hr_euclid.cpp ../../seqgen/hr_gridseq.cpp ../../seqgen/hr_stepfollow.cpp
HRSRC = ../../seqgen/hr.cpp ../../seqgen/hr_stab.cpp ../../seqgen/hr_arpseq.cpp ../../seqgen/hr_chordarp.cpp ../../seqgen/hr_bass.cpp ../../seqgen/hr_euclid.cpp ../../seqgen/hr_gridseq.cpp ../../seqgen/hr_stepfollow.cpp
GLUE = ../source/seqTracksCore.cpp ../source/seqCut.cpp ../interface/seqSession/cutLayer.cpp
EXE := $(if $(filter Windows_NT,$(OS)),.exe,)

.PHONY: test clean preview amp-preview pitch-preview grid-preview
test: build/seq_cpu_tests$(EXE) build/seq_glue_tests$(EXE) build/seq_perform_tests$(EXE) build/seq_mod_tests$(EXE) build/seq_play_tests$(EXE) build/seq_fx_tests$(EXE) build/seq_plinky_tests$(EXE) build/seq_trace_tests$(EXE) build/seq_amp_tests$(EXE) build/seq_warp_tests$(EXE) build/seq_stage_tests$(EXE) build/seq_gridstage_tests$(EXE) build/seq_br_tests$(EXE) build/seq_pitchdevice_tests$(EXE) build/seq_sampmem_tests$(EXE)
	build/seq_cpu_tests$(EXE)
	build/seq_glue_tests$(EXE)
	build/seq_perform_tests$(EXE)
	build/seq_mod_tests$(EXE)
	build/seq_play_tests$(EXE)
	build/seq_fx_tests$(EXE)
	build/seq_plinky_tests$(EXE)
	build/seq_trace_tests$(EXE)
	build/seq_amp_tests$(EXE)
	build/seq_stage_tests$(EXE)
	build/seq_gridstage_tests$(EXE)
	build/seq_pitchdevice_tests$(EXE)
	build/seq_warp_tests$(EXE)
	build/seq_br_tests$(EXE)
	build/seq_sampmem_tests$(EXE)

build/seq_glue_tests$(EXE): test_main.cpp test_session.cpp test_cut.inc test_br.inc ../source/seqBeatRepeatLogic.h ../source/seqBeatRepeat.h ../interface/seqSession/brDraw.h ../libs/Audio/beat_repeat_core.h ../source/seqSessionLogic.h ../source/seqClipsLogic.h ../source/seqCut.h ../source/seqCutLogic.h ../interface/seqSession/cutLayer.h ../interface/seqSession/cutSessionLayer.h ../interface/seqSession/laneDraw.h ../interface/seqSession/mlrDraw.h ../source/seqMlrLogic.h ../source/seqCueCache.h ../source/seqPalette.h ../interface/seqSession/laneView.h ../source/seqTracksCore.h ../source/seqTracksLogic.h ../source/seqTracksRec.h ../source/seqRecLogic.h test_rec.inc test_hr.inc test_plinkyseq.inc ../source/seqPlinkySeqLogic.h ../source/seqPlinkyUiLogic.h ../source/seqTraceStages.h $(GLUE) $(SEQ)
	mkdir -p build
	$(CXX) $(CXXFLAGS) -I../libs/Audio test_main.cpp test_session.cpp $(GLUE) $(SEQ) -o $@

build/seq_sampmem_tests$(EXE): test_sampmem.cpp ../source/sampleMemLogic.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) test_sampmem.cpp -o $@

build/seq_perform_tests$(EXE): test_perform.cpp ../source/seqPerformLogic.h ../source/seqModLogic.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) test_perform.cpp -o $@

build/seq_mod_tests$(EXE): test_mod.cpp ../source/seqMemLogic.h ../source/seqModLogic.h ../source/seqSidecarLogic.h ../source/seqClipsLogic.h ../source/seqSongLogic.h ../source/seqTracksLogic.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) test_mod.cpp -o $@

# PNG previews of the CUT lane screen (plan 005 R4): draw ops from the host test, rasterised by lane_preview.py (needs Pillow)
preview: build/seq_glue_tests$(EXE)
	mkdir -p build/preview
	SEQ_LANE_PREVIEW=build/preview build/seq_glue_tests$(EXE) > /dev/null
	python lane_preview.py build/preview
	$(MAKE) amp-preview

# PNG previews of the amplitude view (plan 014 B; needs Pillow)
amp-preview: build/seq_amp_tests$(EXE)
	mkdir -p build/preview
	SEQ_AMP_PREVIEW=build/preview build/seq_amp_tests$(EXE) > /dev/null
	python amp_preview.py build/preview

# PNG previews of the clip launcher with and without scene colours (plan 017): the real clipGrid.cpp against stubbed display list macros (needs Pillow)
grid-preview: preview_grid.cpp gridstub/FT812.h ../interface/seqSession/clipGrid.cpp ../source/seqPalette.h ../source/seqSessionLogic.h
	mkdir -p build/preview
	$(CXX) $(CXXFLAGS) -Igridstub -I../interface preview_grid.cpp -o build/preview_grid$(EXE)
	build/preview_grid$(EXE) build/preview
	python grid_preview.py build/preview

# PNG previews of the pitch view (plan 015 V): the real engine through ring, follower and drawing (needs Pillow)
pitch-preview: build/seq_trace_tests$(EXE)
	mkdir -p build/preview
	SEQ_PITCH_PREVIEW=build/preview build/seq_trace_tests$(EXE)
	python amp_preview.py build/preview

build/seq_play_tests$(EXE): test_play.cpp ../source/seqPlayLogic.h ../source/seqTracksLogic.h $(HRSRC)
	mkdir -p build
	$(CXX) $(CXXFLAGS) test_play.cpp $(HRSRC) -o $@

build/seq_fx_tests$(EXE): test_fx.cpp ../source/seqFxDsp.h ../source/seqFxLogic.h ../source/seqSidecarLogic.h ../libs/Audio/Polyverb/ring_reverb.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) test_fx.cpp -o $@

# plan 009 S: synth voice engine (libs/Audio/PlinkyVoice, header only). -O2: the cost lines of the test are only meaningful optimised
build/seq_plinky_tests$(EXE): test_plinky.cpp ../libs/Audio/PlinkyVoice/plinky_voice.h ../libs/Audio/PlinkyVoice/plinky_params.h ../libs/Audio/PlinkyVoice/plinky_wavetable.h ../libs/Audio/PlinkyVoice/plinky_governor.h ../source/seqSidecarLogic.h ../source/seqPlinkyMorphLogic.h test_plinky3.inc test_plinky4.inc ../source/seqInstOrderLogic.h ../source/seqPlinkyUiLogic.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -O2 -I../libs/Audio/PlinkyVoice -I../libs/Audio test_plinky.cpp -o $@

# plan 013: pitch trace (engine ring, follower, layout, display list size), amplitude view, launcher budgets
build/seq_trace_tests$(EXE): test_trace.cpp ../libs/Audio/PlinkyVoice/plinky_voice.h ../source/seqPitchTraceLogic.h ../source/seqPalette.h ../source/seqSessionLogic.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -O2 -I../libs/Audio/PlinkyVoice test_trace.cpp -o $@

# plan 014 B: amplitude view (dB table, bucket cascade, age -> x, per-track lanes, layout / display-list budget)
build/seq_amp_tests$(EXE): test_amp.cpp ../source/seqAmpViewLogic.h ../source/seqPalette.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -O2 test_amp.cpp -o $@

# v0.3.9: device path of the trace views (engine ring / peak rings at the 40 ms main loop cadence, two display stages, FT81x command FIFO model)
build/seq_stage_tests$(EXE): test_stages.cpp ../source/seqTraceStages.h ../source/seqAmpViewLogic.h ../source/seqPitchTraceLogic.h ../libs/Audio/PlinkyVoice/plinky_voice.h ../source/seqPalette.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -O2 -I../libs/Audio/PlinkyVoice test_stages.cpp -o $@

# v0.4.1: the pitch view on the device call sequence (the real pitchTrace.cpp / traceControl.cpp against devstub/: display.cpp step machine,
# 8172 byte frame; the real engine block by block; the module update as Interface::update calls it, read from the module sources)
build/seq_pitchdevice_tests$(EXE): test_pitchdevice.cpp devstub/FT812.h devstub/display.h devstub/displayControls.h devstub/seqTracks.h devstub/cr_section_macros.h ../interface/seqPlay/pitchTrace.cpp ../interface/seqPlay/pitchTrace.h ../interface/seqPlay/traceControl.cpp ../interface/seqPlay/traceControl.h ../source/seqPitchTraceLogic.h ../source/seqTraceStages.h ../source/seqPlinky.h ../libs/Audio/PlinkyVoice/plinky_voice.h ../interface/instrumentEditor/fIinstrEdit.cpp ../interface/instrumentEditor/gInstrEdit.cpp ../interface/patternEditor/fPatternEdit.cpp ../interface/seqPlay/seqPlayOverlay.cpp
	mkdir -p build
	$(CXX) -Idevstub $(CXXFLAGS) -Wno-format-truncation -I../interface -I../libs/Audio -I../libs/Audio/PlinkyVoice test_pitchdevice.cpp -o $@

# v0.3.9: the launcher grid (the real clipGrid.cpp against fifostub/) in two display stages, FT81x command FIFO sizes
build/seq_gridstage_tests$(EXE): test_gridstages.cpp fifostub/FT812.h fifostub/display.h ../interface/seqSession/clipGrid.cpp ../interface/seqSession/clipGrid.h ../source/seqSessionLogic.h ../source/seqTraceStages.h ../source/seqPalette.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Ififostub -I../interface test_gridstages.cpp -o $@

# plan 016 B: Warp (time-stretch to the project tempo): grain engine (libs/Audio/warp_core.h, header only) + settings / sidecar. -O2: the cost lines are only meaningful optimised
build/seq_warp_tests$(EXE): test_warp.cpp ../libs/Audio/warp_core.h ../source/seqWarpLogic.h ../source/seqSidecarLogic.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -O2 -I../libs/Audio test_warp.cpp -o $@

# plan 021: beat repeat DSP core (libs/Audio/beat_repeat_core.h, header only). -O2: the cost line is only meaningful optimised
build/seq_br_tests$(EXE): test_beatrepeat.cpp ../libs/Audio/beat_repeat_core.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -O2 -I../libs/Audio test_beatrepeat.cpp -o $@

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
	for f in filter_variable amplifier monoToStereo mixer dspApplyGain analyze_rms; do $(CXX) $(DELAYINC) $(CXXFLAGS) -std=gnu++11 -c ../libs/Audio/$$f.cpp -o build/$$f.o || exit 1; done
	for f in ref_amplifier ref_monoToStereo; do $(CXX) $(DELAYINC) -Iref/voice $(CXXFLAGS) -std=gnu++11 -c ref/voice/$$f.cpp -o build/$$f.o || exit 1; done
	$(CXX) $(DELAYINC) -I../AudioEngine/ConstantValues -Iref/limiter $(CXXFLAGS) -std=gnu++11 -c ref/limiter/ref_limiter.cpp -o build/ref_limiter.o
	$(CXX) $(DELAYINC) $(CXXFLAGS) -std=gnu++11 -Iref/delay -Iref/limiter -Iref/voice test_cpu.cpp build/cpu_reverb_ref.o build/cpu_reverb_new.o build/effect_shortDelay.o build/ref_shortDelay.o build/audio_stream_stub.o build/effect_limiter.o build/ref_limiter.o build/filter_variable.o build/amplifier.o build/monoToStereo.o build/mixer.o build/dspApplyGain.o build/ref_amplifier.o build/ref_monoToStereo.o build/analyze_rms.o -o $@
