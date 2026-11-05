# Third-Party Notices & Attributions

Polyend Tracker Community Edition (CE) incorporates or links against the following third-party open-source and binary components:

---

## 1. ARM CMSIS & CMSIS-DSP
* **Location**: `platforms/imx/CMSIS/`, `platforms/mk66/3rdparty/teensy_core/`
* **License**: Apache License 2.0 / BSD-3-Clause
* **Copyright**: Copyright (c) ARM Limited. All rights reserved.

## 2. NXP MCUXpresso SDK (Kinetis K66 & i.MX RT1050/RT1060)
* **Location**: `platforms/imx/drivers/`, `platforms/imx/device/`, `platforms/imx/usb/`, `platforms/imx/sdmmc/`, `platforms/mk66/3rdparty/SD/`, `platforms/mk66/3rdparty/I2C/`, `platforms/mk66/3rdparty/SDRAM/`
* **License**: BSD-3-Clause
* **Copyright**: Copyright (c) Freescale Semiconductor, Inc. / Copyright 2016-2022 NXP.

## 3. FatFs - Generic FAT Filesystem Module
* **Location**: `platforms/imx/fatfs/`, `platforms/mk66/3rdparty/SD/fatfs/`
* **License**: FatFs License (BSD-style)
* **Copyright**: Copyright (C) ChaN, 2019. All rights reserved.

## 4. Teensy Cores & Teensy Audio Library (PJRC)
* **Location**: `platforms/mk66/3rdparty/teensy_core/`, `platforms/mk66/3rdparty/Audio/`, `core/libs/Audio/`
* **License**: MIT / PJRC Permissive License
* **Copyright**: Copyright (c) 2014-2018 Paul Stoffregen, PJRC.COM, LLC.

## 5. Liberation Sans Fonts (UI Bitmap Font Source)
* **Location**: `core/libs/fonts/LiberationSans-*_L4.{h,cpp}` (4-bit `L4` antialiased glyph bitmaps)
* **License**: SIL Open Font License 1.1 (OFL-1.1)
* **Copyright**: Digitized data copyright (c) 2010 Google Corporation; Copyright (c) 2012 Red Hat, Inc.

## 6. Embedded Template Library (ETL)
* **Location**: `util/3rdparty/etl/`
* **License**: MIT License
* **Copyright**: Copyright (c) 2016 jwellbelove (John Wellbelove).

## 7. magic_enum
* **Location**: `util/3rdparty/magic_enum/`
* **License**: MIT License
* **Copyright**: Copyright (c) 2019 - 2022 Daniil Goncharov.

## 8. FastCRC
* **Location**: `platforms/mk66/3rdparty/FastCRC/`, `platforms/imx/3rdparty/FastCRC/`
* **License**: MIT / GPLv3 dual-licensed (used under MIT/BSD terms where applicable)
* **Copyright**: Copyright (c) 2014-2016 Frank Boesing.

## 9. Polyend Precompiled Synth & DSP Libraries (`libPolySynth.a`, `libPolyDSP.a`)
* **Location**: `synth/lib/`
* **License**: Polyend Precompiled Synthesizer & DSP Library License (`synth/lib/LICENSE-PROPRIETARY.txt`)
* **Copyright**: Copyright (c) 2023-2026 Polyend Sp. z o.o. All rights reserved.

## 10. Reverb algorithm of the Plinky firmware (reverb type "Plinky")
* **Location**: `core/libs/Audio/Polyverb/ring_reverb.h` (a port of `Reverb2` in `sw/Core/Src/plinky.c` of <https://github.com/plinkysynth/plinky_public>)
* **License**: MIT License (the full notice is kept at the top of the ported file)
* **Copyright**: Copyright (c) the authors of the Plinky firmware (the repository states: "The rest of the software of plinky is licensed under the MIT License")
* **Note**: only the reverb algorithm is used. The Plinky name, logo and front panel design are not part of the MIT grant; this firmware is not a Plinky product. The name appears only as the label of the reverb type, to credit the algorithm.

## 11. Synth voice of the Plinky firmware (the "Plinky" synth voice, Original Tracker)
* **Location**: `core/libs/Audio/PlinkyVoice/plinky_voice.h` (the oscillator model, integer phase polyBLEP saw, pulse from two saws, low pass gate recursion, envelope smoothing and per-block ramps follow `RunVoice` in `sw/Core/Src/plinky.c` of <https://github.com/plinkysynth/plinky_public>)
* **License**: MIT License (the full notice is kept at the top of the ported file)
* **Copyright**: Copyright (c) the authors of the Plinky firmware (the repository states: "The rest of the software of plinky is licensed under the MIT License")
* **Note**: only the base synth voice is used (no sampler, arpeggiator, sequencer, effects, wavetables or touch strips). Voice allocation, event queue, pitch maths, LFO, parameter set, scaling and the CPU optimisations are written for the Tracker and are part of the sequencer tracks mod (PolyForm Noncommercial 1.0.0). The Plinky name, logo and front panel design are not part of the MIT grant; this firmware is not a Plinky product. The name appears only as the credit of the algorithm.
