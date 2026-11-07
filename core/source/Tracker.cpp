#include "Platform.h"
#include "Profiler.h"
#include "seqCpu.h" // plan 022 P1
#include "seqMasterRec.h" // plan 030 A
#include "TrackerImpl.h"
#include "mtWavetableEnvelopes.h"
#include "core/interface.h"
#include "elapsedMillis.h"
#include "seqTracks.h"
#include "seqLauncher.h"
#include "seqPlinky.h"
#include "masterParams/ampView.h" // v0.4.3: the amplitude history runs in every module
#include "fileManager.h"
#include "mtAudioEngine.h"
#include "mtAudioExport.h"
#include "mtConfig.h"
#include "mtHardware.h"
#include "mtMidi.h"
#include "mtPadsBacklight.h"
#include "mtSequencer.h"
#include "mtSleep.h"
#include "mtSliceDetector.h"

#include <cr_section_macros.h>

//-----------------------------------  EXTERNAL RAM CONFIGURATION --------------------------------
// clang-format off
__attribute__((section(".sample_memory"), used)) int16_t externalRamSampleMemory[SAMPLE_MEMORY_SIZE / 2];
__attribute__((section(".delay_memory"), used)) int16_t externalRamDelayBuff[DELAY_MEMORY_SIZE / 2];
#ifdef PLATFORM_IMX // plan 010: only the Tracker Plus / Mini use it (second buffer of the stock reverb); the Original gave the 32 KB back
__attribute__((section(".reverb_memory"), used)) uint16_t externalRamReverbBuff[REVERB_MEMORY_SIZE / 2];
#endif
__attribute__((section(".scratch_memory"), aligned(32), used)) uint8_t externalRamScratchBuff[SHARED_SCRATCH_MEMORY_SIZE];

// clang-format on
int16_t* externalRamEffectsBuff = externalRamSampleMemory + (EDIT_BUFF_OFFSET / 2);
int16_t* externalRamSampleMemoryBegin = externalRamSampleMemory;

//--------------------------------------------------------------------------------------------------------------
static TrackerImpl instance;

extern void onPowerButtonChange(int value);
extern void onSDCardSlotChange(bool state);

void startDisplay(Platform* platform)
{
    [[maybe_unused]] bool powerButtonState = platform->getPowerButtonState();

    display.begin(platform->getDisplayDriver(), platform->getTimer());
#ifdef PLATFORM_IMX
    // delay to hide white screen before driver initialize the screen
    platform->getTimer()->delay(200);
#endif

#ifdef TRACKER_MINI
    // only in release build mode (always start device immediately in debug mode)
#if !defined(DEBUG) && !defined(FORCED_AUTOSTART)
    if (platform->readForceStartUp() == false &&
        (!powerButtonState || platform->getBattery()->isEmpty()))
    {
        Battery* battery = platform->getBattery();
        display.showBatteryScreen(
          battery->isEmpty(), battery->getCharging(), battery->getLevel(), battery->getVoltage());
        platform->getTimer()->delay(100);
        display.turnOn();
        display.showBatteryScreen(
          battery->isEmpty(), battery->getCharging(), battery->getLevel(), battery->getVoltage());
        platform->getTimer()->delay(2000);
        platform->setPowerMode(false);
    }
#endif
#endif

    display.turnOn();
}

Tracker* Tracker::getInstance()
{
    return &instance;
}

TrackerImpl* TrackerImpl::getInstance()
{
    return &instance;
}

Platform* TrackerImpl::getPlatform()
{
    return platform;
}

#ifdef FEATURE_SYNTH
Synth& TrackerImpl::getSynth()
{
    return synth;
}
#endif

const char* Tracker::getVersion()
{
    return "1.0";
}

void TrackerImpl::init(Platform* platform)
{
    this->platform = platform;

    initWavetableEnvelopes();
#ifdef FEATURE_PLINKY
    seqPlinkyInit(); // v0.3.5: the morph state in EXTERNAL_RAM is constructed here (the SDRAM is up), not by a static constructor
#endif

    Timer* timer = getPlatform()->getTimer();
    elapsedMicros::init(timer);
    elapsedMillis::init(timer);
    elapsedMicrosAudio::init(timer);
    elapsedMillisAudio::init(timer);
#ifdef PLATFORM_IMX
    AudioStream::setUsbAudio(platform->getUsbAudio());
#endif

    midiInit(platform->getUartMidi(), platform->getUsbMidi());

    platform->setPowerButtonCallback(onPowerButtonChange);
    platform->getFileSystem()->setMountEventCallback(onSDCardSlotChange);

#ifdef FEATURE_SYNTH
    synth.init(platform, [this]() { return sequencer.getCurrentTempo(); });

    engine.init(platform, &synth);
#else
    engine.init(platform, nullptr);
#endif
    sequencer.init();
    sequencer.setCrcCalculator(platform->getCrcCalculator());
    padsBacklight.init(500, platform->getLeds());
    exporter.init(platform->getFileSystem());
    seqmasterrec::init(platform->getFileSystem()); // plan 030 A: the master recording ring (EXTERNAL_RAM raw storage)
    Profiler::getInstance()->init();
    seqcpu::init(); // plan 022 P1: cycle counter for the CPU monitor
    newFileManager.setCrcCalculator(platform->getCrcCalculator());

    initConfig(platform);
    // UI init at the end
#ifdef FEATURE_SYNTH
    mtInterface.begin(platform, &synth);
#else
    mtInterface.begin(platform, nullptr);
#endif

    bool stereoSampleMode = false;
#ifdef PLATFORM_IMX
    stereoSampleMode = true;
#endif

#ifdef FEATURE_SYNTH
    newFileManager.init(platform->getFileSystem(), &synth, stereoSampleMode);
#else
    newFileManager.init(platform->getFileSystem(), nullptr, stereoSampleMode);
#endif

    interfaceGlobals.devMode = platform->getFileSystem()->exists("/.develop");
}

void TrackerImpl::update()
{
    if (lowPower.getLowPowerState() == shutdownStateSleep)
    {
        if (seqmasterrec::active())
            seqmasterrec::stop(); // plan 030 A: the file is closed before the card goes away
        return;
    }

    const uint32_t perfT0 = seqcpu::cyc(); // plan 022 P1: main loop pass time
    engine.updateSdStream();
    seqmasterrec::update(); // plan 030 A: at most one 16 KB chunk of the master recording, only while recording
    display.update();
    engine.updateSdStream();
    padsBacklight.update();
    exporter.update();
    sliceDetector.update();
    newFileManager.update();
    mtInterface.update();
    engine.updateSdStream();
    sequencer.update();
    seqTracksUpdate();
    seqSessionUpdate();
    ampView.tick(); // v0.4.3: the amplitude history of the Master amp view, always (cheap; nothing drawn unless the view is open)
    Profiler::getInstance()->sampleHeap(); // plan 010: lowest free heap (shown on the Config page)
    seqcpu::passEnd(seqcpu::cyc() - perfT0);
}

void TrackerImpl::updateAudioBuffers(void* left, void* right, int length)
{
    if (AudioStream::renderMode)
    {
        return;
    }
    if (platform != nullptr)
    {
        midiSetAudioBlockStartUs(platform->getTimer()->micros());
    }
    Profiler::getInstance()->startMeasuringAudio();
    const uint32_t perfT0 = seqcpu::cyc(); // plan 022 P1
    midiUpdate();

    // Exact hardware I2S sample rate:
    // - IMX (Tracker+ and Tracker Mini): PLL4 = 24 MHz * (37 + 15168/24000) / 2 / 4 / 10 / 256 = 44100.0 Hz
    // - MK66 (Original Tracker): F_CPU = 180 MHz * 16 / 255 / 256 = 44117.6470588 Hz
#ifdef PLATFORM_IMX
    static constexpr float kAudioSampleRate = 44100.0f;
#else
    static constexpr float kAudioSampleRate = 44117.6470588f;
#endif

    float updateIntervalSeconds = length / kAudioSampleRate;
    sequencer.tick(updateIntervalSeconds);
    const uint32_t perfT1 = seqcpu::cyc();

    engine.update();
    const uint32_t perfT2 = seqcpu::cyc();

    engine.updateAudioBuffers(left, right, length);
    Profiler::getInstance()->stopMeasuringAudio();
    if (seqcpu::active()) // plan 022 P1: the control-rate and sequencer parts of the block (the streams are timed in update_all)
    {
        seqcpu::stageAdd(seqcpu::S_SEQ, perfT1 - perfT0);
        seqcpu::stageAdd(seqcpu::S_CTRL, perfT2 - perfT1);
    }
    seqcpu::blockEnd(seqcpu::cyc() - perfT0);
}

void TrackerImpl::updateInputAudioBuffers(void* dataI2s, void* dataUsb, int length)
{
    engine.updateInputAudioBuffers(dataI2s, dataUsb, length);
}

void (*TrackerImpl::getStartDisplayFunc())(Platform*)
{
    return startDisplay;
}
