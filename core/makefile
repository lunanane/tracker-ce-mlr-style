# Variables to provide (example values):
# TOOLCHAIN=/usr/local/bin/arm-none-eabi-
# PLATFORM=MK66 (MK66 and IMX are valid values)
# DEBUG=1 (any value turns on)

# Target configuration
TARGET		:= libTracker.a

# Build environment setup
.DEFAULT_GOAL := build

BUILD_DIR   := build-$(PLATFORM)-$(PRODUCT)
OBJ_DIR 	:= $(BUILD_DIR)/obj/
RM 			:= rm -rf

CPP_SRCS 	+= $(shell find ./source/ -name *.cpp)
CPP_SRCS 	+= $(shell find ./libs/ -name *.cpp)
CPP_SRCS 	+= $(shell find ./interface/ -name *.cpp)
CPP_SRCS 	+= $(shell find ./effector/ -name *.cpp)
CPP_SRCS 	+= $(shell find ./FileManager/ -name *.cpp)
CPP_SRCS 	+= $(shell find ./Exporter/ -name *.cpp)
CPP_SRCS 	+= $(shell find ./AudioEngine/ -name *.cpp)
CPP_SRCS 	+= $(shell find ./synth/ -name *.cpp)
C_SRCS 	 	+= $(shell find ./libs/ -name *.c)

# sequencer tracks mod: the pure C++ library in ../seqgen is built into the core (objects under obj/seqgen/)
SEQGEN_DIR	?= ../seqgen
SEQGEN_SRCS	:= chordarp mono playhead hr hr_stab hr_arpseq hr_chordarp hr_bass hr_euclid hr_gridseq

_CPP_OBJS	= $(patsubst ./%.cpp, %.o, $(CPP_SRCS))
_C_OBJS		= $(patsubst ./%.c, %.o, $(C_SRCS))

OBJS		+= $(addprefix $(OBJ_DIR)/, $(_CPP_OBJS))
OBJS		+= $(addprefix $(OBJ_DIR)/, $(_C_OBJS))
OBJS		+= $(addprefix $(OBJ_DIR)/seqgen/, $(addsuffix .o, $(SEQGEN_SRCS)))

OBJS_SORTED := $(sort $(OBJS))
OBJ_DIRS	:= $(dir $(OBJS_SORTED))
DEPS		:= $(OBJS_SORTED:.o=.d)

INCLUDES=-I./include -I./config -I./source -I./synth -I./libs -I./libs/Audio -I./libs/ElapsedMillis -I./libs/SerialFlash -I./libs/SPI -I./libs/Audio/utility -I./libs/FT812 -I./libs/fonts -I./libs/NewDisplay -I./interface -I./effector -I./FileManager -I$(SEQGEN_DIR) -I./Exporter -I./AudioEngine -I./libs/Audio/Polyverb -I./AudioEngine/ConstantValues -I./AudioEngine/MasterEnhancer -I${POLYUTIL_DIR}/include -I${POLYUTIL_DIR}/3rdparty -I${POLYSYNTH_DIR}/include  
COMPILER_OPTIONS=-MMD -MP -fno-common -g -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -fstack-usage -specs=nano.specs -Wall -Wextra -c -ffunction-sections -fdata-sections -ffreestanding -fno-builtin 
ifdef DEBUG
COMPILER_OPTIONS+=-Og -DDEBUG
else
COMPILER_OPTIONS+=-O2 -DNDEBUG
endif

CPP_OPTIONS=-std=gnu++20 -felide-constructors -includecstdint -fno-rtti -fno-exceptions 
CC_OPTIONS=

DEFINES+=-DAUDIO_PERFORMANCE_INFO_ENABLED -DMEMORY_USAGE_INFO_ENABLED -D__NEWLIB__ -DKINETISK -D__MK66FX1M0__ -DF_CPU=180000000 -DUSB_MIDI -DTEENSYDUINO=142 -DARDUINO=10805 -DLAYOUT_US_ENGLISH -DCPU_MK66FX1M0VMD18
POLYUTIL_DIR ?= ../util
POLYSYNTH_DIR ?= ../synth
-include ../version.mk
DEFINES += $(FW_VERSION_DEFINES) $(CUSTOM_DEFINES)

ifeq ($(PLATFORM), MK66)
COMPILER_OPTIONS+=-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 
else ifeq ($(PLATFORM), IMX)
COMPILER_OPTIONS+=-mcpu=cortex-m7 -mfpu=fpv5-sp-d16 -mfp16-format=ieee
DEFINES+=-DPLATFORM_IMX -DFEATURE_SYNTH
else 
$(error Invalid PLATFORM value $(PLATFORM))
endif

ifeq ($(PRODUCT), MINI)
DEFINES+=-DTRACKER_MINI
else ifeq ($(PRODUCT), ORIGINAL)
else ifeq ($(PRODUCT), PLUS)
else 
$(error Invalid PRODUCT value $(PRODUCT))
endif




# sequencer tracks mod: on MK66 the mod's screen/UI code (not the audio path) is built for size, so the image stays
# below 0xF0000 (960 KB): v0.2 at 0xF04B0 was refused by the original Tracker's updater, v0.1 at 0xE73D8 flashed fine
# v0.2.4 (plan 007): the stock Master page (interface/masterParams, configEditor, sampleImporter, projectEditor, instrumentEditor, sampleEditor: stock UI only, no audio path) is built for size too, to make room for the FX filters
ifeq ($(PLATFORM), MK66)
$(OBJ_DIR)/interface/seqPlay/%.o $(OBJ_DIR)/interface/seqSession/%.o $(OBJ_DIR)/interface/seqPerform/%.o $(OBJ_DIR)/interface/masterParams/%.o $(OBJ_DIR)/interface/configEditor/%.o $(OBJ_DIR)/interface/sampleImporter/%.o $(OBJ_DIR)/interface/projectEditor/%.o $(OBJ_DIR)/interface/instrumentEditor/%.o $(OBJ_DIR)/interface/sampleEditor/%.o: COMPILER_OPTIONS += -Os
endif

# plan 008 B: on MK66 ALL non-audio stock code (screens, file manager, display lib, fonts, exporter, theme/config helpers) is built
# for size too. Kept at -O2: everything in AudioEngine/, libs/Audio/, effector/, synth/, seqgen/ and the sequencer sources
# (source/mtSequencer*, seq*), and the three UI files that hold AUDIO_CORE_RAM (interrupt/audio-timed) functions.
ifeq ($(PLATFORM), MK66)
OS_DIRS		:= interface FileManager Exporter libs/NewDisplay libs/fonts libs/images libs/FT812
OS_FILES	:= source/FileBrowser source/ThemeManager source/Themes source/mtRandomNameGenerator source/mtGridEditor source/mtConfig source/mtSliceDetector source/mtSliceManager
OS_KEEP_O2	:= interface/core/songTimer interface/performanceMode/fPerformance FileManager/fileManagerSong
OS_OBJS		:= $(filter $(addprefix $(OBJ_DIR)/,$(addsuffix /%,$(OS_DIRS)) $(addsuffix .o,$(OS_FILES))),$(OBJS))
OS_OBJS		:= $(filter-out $(addprefix $(OBJ_DIR)/,$(addsuffix .o,$(OS_KEEP_O2))),$(OS_OBJS))
$(OS_OBJS): COMPILER_OPTIONS += -Os
endif

# Build rules

$(OBJ_DIR)/%.o: %.cpp
	$(TOOLCHAIN)c++ $(DEFINES) $(INCLUDES) $(CPP_OPTIONS) $(COMPILER_OPTIONS) -o "$@" "$<"

# the HYPER_RASP script ports run per clock pulse / pad press, never per sample: -Os
$(OBJ_DIR)/seqgen/hr%.o: COMPILER_OPTIONS += -Os

$(OBJ_DIR)/seqgen/%.o: $(SEQGEN_DIR)/%.cpp
	$(TOOLCHAIN)c++ $(DEFINES) $(INCLUDES) $(CPP_OPTIONS) $(COMPILER_OPTIONS) -o "$@" "$<"

$(OBJ_DIR)/%.o: %.c
	$(TOOLCHAIN)gcc $(DEFINES) $(INCLUDES) $(CC_OPTIONS) $(COMPILER_OPTIONS) -o "$@" "$<"

$(TARGET): $(OBJS)
	$(file >$(BUILD_DIR)/objs.rsp,$(OBJS_SORTED))
	$(TOOLCHAIN)ar -r "$(BUILD_DIR)/$(TARGET)" @$(BUILD_DIR)/objs.rsp
	$(MAKE) --no-print-directory post-build
	
# Main rules

build:
	$(MAKE) --no-print-directory pre-build
	$(MAKE) --no-print-directory main-build

main-build: $(TARGET)

pre-build:
	$(foreach dir,$(sort $(OBJ_DIRS)),mkdir -p $(dir);)
post-build: 

clean:
	-$(RM) $(BUILD_DIR)
	
-include $(DEPS)

.PHONY: build
