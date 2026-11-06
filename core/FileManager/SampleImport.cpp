#include "SampleImport.h"
#include "AudioDataUtilities.h"
#include "Platform.h"
#include "fileManager.h"
#include "fileManagerDefs.h"
#include "fileTransfer.h"
#include "mtAudioEngine.h"

#include "VoiceStreamManager.h"
#include "log.h"
#include "sampleMemLogic.h"

#include <cstring>

constexpr uint32_t lofiSampleRate = targetSampleRate / 2;

extern uint8_t externalRamFileManagerSrcBuff[]; // 32640 B, input chunk
extern uint8_t externalRamFileManagerBuffL[];   // 32640 B, converted L (first half) + R (second half)
constexpr uint32_t kStreamSrcChunkBytes = 32640;
constexpr uint32_t kStreamOutHalfFrames = 8160; // frames per channel in one converted chunk

__NOINIT(EXTERNAL_RAM)
static FileManagerOperation::SampleLoadData sampleImportDataBuff[48];

void SampleImport::update() // fmImportSamplesToWorkspace
{
    SampleImportStep nextStep = operationStep;

    switch (operationStep)
    {
        case SampleImportStep::initStep:
        {
            importSamplesToWorkspaceInit();
            memset(sampleImportDataBuff, 0, sizeof(sampleImportDataBuff));
            loadImportingSamplesData(sampleImportDataBuff,
                                     newFileManager.getExplorerPath(),
                                     newFileManager.getExplorerList());
            nextStep = SampleImportStep::moveMemory;
            break;
        }
        case SampleImportStep::moveMemory:
        {
            uint32_t newSamplesSize =
              newFileManager.getSampleMemorySize(importStartSlot, importEndSlot);

            newFileManager.moveSampleMemory(
              newFileManager.calcFirstSlotToMoveInMemory(importEndSlot + 1),
              newSamplesSize - oldSamplesSize);

            nextStep = SampleImportStep::loadSampleData;
            break;
        }
        case SampleImportStep::loadSampleData:
        {
            bool result =
              loadSampleDataToMemeory(currentInstrument, &sampleImportDataBuff[currentInstrument]);
            if (result)
            {
                memoryToImportDone += fileTransfer.getBytesComplited();
                fileTransfer.clearBytesComplited();
                nextStep = SampleImportStep::saveInstrumentStructToWorkspace;
            }
            break;
        }
        case SampleImportStep::saveInstrumentStructToWorkspace:
        {
            newFileManager.setInstrumentStructChanged(currentInstrument);
            newFileManager.saveInstrumentStructToWorkspace(currentInstrument);
            nextStep = SampleImportStep::saveInstrumentSampleToWorkspace;
            break;
        }
        case SampleImportStep::saveInstrumentSampleToWorkspace:
        {
            bool result = newFileManager.saveSampleDataToWorkspace(currentInstrument);
            if (result)
            {
                engine.clearBlockedInstrument(currentInstrument);
                if (currentInstrument < importEndSlot &&
                    currentInstrument < kSampleInstrumentsCount - 1)
                {
                    currentInstrument++;
                    nextStep = SampleImportStep::loadSampleData;
                }
                else
                {
                    nextStep = SampleImportStep::finishStep;
                }
                memoryToImportDone += fileTransfer.getBytesComplited();
                fileTransfer.clearBytesComplited();
            }
            break;
        }
        case SampleImportStep::finishStep:
        {
            importSamplesToWorkspaceFinish();
            break;
        }
        default: stopOperationWithError(fmImportSamplesError); break;
    }

    operationStep = nextStep;
}

float SampleImport::getProgress()
{
    float progress = 0;
    float complited = memoryToImportDone;

    switch (operationStep)
    {
        case initStep:
        {
            progress = 0.01;
            break;
        }
        case moveMemory:
        {
            progress = 0.05;
            break;
        }
        case saveInstrumentStructToWorkspace:
        case saveInstrumentSampleToWorkspace:
        case loadSampleData:
        {
            complited += fileTransfer.getBytesComplited();
            float memoryProgress =
              memoryToImportTotal > 0 ? (complited / (float)memoryToImportTotal * 0.9f) : 0;
            progress = 0.1 + memoryProgress;
            break;
        }
        default: break;
    }

    // LOGF("SImport op: %d, pr: %d | Done: %d / %d \n",
    //     operationStep,
    //     (int)(progress * 100),
    //     (int)complited,
    //     memoryToImportTotal);

    return progress < 1.0f ? progress : 1.0f;
}

//=================================================================================================
//=================================================================================================
void SampleImport::importSamplesToWorkspaceInit()
{
    for (int instrSlot = importStartSlot; instrSlot <= importEndSlot; instrSlot++)
    {
        engine.setBlockedInstrument(instrSlot);
    }

    oldSamplesSize = newFileManager.getSampleMemorySize(importStartSlot, importEndSlot);

    mtProject.used_memory -= oldSamplesSize;

    sampleLoadState = 0;
    importPlannedBytes = 0;
    streamStep = 0;
    streamConvertClose();

    memoryToImportTotal = 0;
    memoryToImportDone = 0;
}

void SampleImport::importSamplesToWorkspaceFinish()
{
    for (int instrSlot = importStartSlot; instrSlot <= importEndSlot; instrSlot++)
    {
        if (instrSlot >= 0 && instrSlot < kSampleInstrumentsCount &&
            mtProject.instrument[instrSlot].isActive)
        {
            char instrPath[PATCH_SIZE];
            snprintf(instrPath, sizeof(instrPath), cWorkspaceInstrumentFileFormatNew, instrSlot + 1);
            newFileManager.demoteRamInstrumentToStream(
              instrSlot, instrPath, sizeof(strInstrumentFile));
        }
    }

    newFileManager.setStatus(fmImportSamplesEnd);
    newFileManager.setCurrentOperation(fmNoOperation);
}

//=================================================================================================
//=================================================================================================
bool SampleImport::importSamplesToProject(uint8_t fileFrom,
                                          uint8_t fileTo,
                                          uint8_t instrumentSlot,
                                          uint16_t dataFrom,
                                          uint16_t dataTo,
                                          bool isLofi,
                                          SampleChannelMode sampleMode)
{
    if (newFileManager.getStatus() != fmIdle)
        return false;
    if (newFileManager.getCurrentOperation() != fmNoOperation)
        return false;
    if (dataFrom >= dataTo || dataFrom > std::numeric_limits<uint16_t>::max() ||
        dataTo > std::numeric_limits<uint16_t>::max())
        return false;

    sampleChannelMode = sampleMode;
    importCurrentFileIdx = fileFrom;
    importDataFrom = dataFrom;
    importDataTo = dataTo;
    importIsLofi = isLofi;

    importExplorerSlotFrom = fileFrom;
    importExplorerSlotTo = fileTo;

    currentInstrument = instrumentSlot;
    importStartSlot = instrumentSlot;
    importEndSlot = instrumentSlot + ((fileTo > fileFrom) ? (fileTo - fileFrom) : 0);
    if (importEndSlot >= kSampleInstrumentsCount)
    {
        importEndSlot = kSampleInstrumentsCount - 1;
    }

    // sprawdzenie czy wystarczy pamieci xxx

    newFileManager.setStatus(fmImportingSamplesToWorkspace);
    operationStep = SampleImportStep::initStep;
    newFileManager.setCurrentOperation(fmImportSamplesToWorkspace);
    return true;
}

// importowanie dowolnego wavaprzez podanie jego sciezki na dowolny slot instrumentu
bool SampleImport::importSampleToProject(char* filePath,
                                         char* fileName,
                                         uint8_t instrumentSlot,
                                         SampleChannelMode sampleMode)
{
    if (newFileManager.getStatus() != fmIdle)
        return false;
    if (newFileManager.getCurrentOperation() != fmNoOperation)
        return false;

    // sprawdz czy plik istnieje
    char test_path[255];
    sprintf(test_path, "%s/%s.wav", filePath, fileName);
    if (!fileSystem->exists(test_path))
        return false;

    if (instrumentSlot >= kSampleInstrumentsCount)
    {
        return false;
    }

    // explorerCurrentPath, explorerList[importCurrentFile]
    // kozrysta z listy plikow explorera
    // ustawia plik do importu na 0 miejscu tablicy explorerList[]
    // i uruchamia import jak w sample laderze
    if (newFileManager.getExplorerList()[0] != nullptr)
    {
        delete newFileManager.getExplorerList()[0];
        newFileManager.getExplorerList()[0] = nullptr;
    }

    uint16_t len = strlen(fileName) + 5; // + .wav + \0
    newFileManager.getExplorerList()[0] = new char[len];
    strcpy(newFileManager.getExplorerList()[0], fileName);
    strcat(newFileManager.getExplorerList()[0], ".wav");

    // wymagane tez ustawienie aktualnego path explorera
    // na lokalizajce wczytywanego pliku
    strcpy(newFileManager.getExplorerPath(), filePath);

    // liczy sie tlyko 0 index w tablicy
    importCurrentFileIdx = 0;

    // default parametry importu
    sampleChannelMode = sampleMode;
    importDataFrom = 0;
    importDataTo = std::numeric_limits<uint16_t>::max();
    importIsLofi = false;

    importExplorerSlotTo = 0;
    importExplorerSlotFrom = 0;

    currentInstrument = instrumentSlot;
    importStartSlot = instrumentSlot;
    importEndSlot = instrumentSlot;

    // reset browsera przy nastepnym uzywaniu
    newFileManager.resetBrowse();

    newFileManager.setStatus(fmImportingSamplesToWorkspace);
    operationStep = SampleImportStep::initStep;
    newFileManager.setCurrentOperation(fmImportSamplesToWorkspace);
    return true;
}

//=================================================================================================
//=================================================================================================
void SampleImport::loadImportingSamplesData(FileManagerOperation::SampleLoadData* sampleImportData,
                                            char* sourceFilePath,
                                            char** sourceFileNameList)
{
    char srcSamplePath[PATCH_SIZE];
    strWavFileHeader sampleHead;

    for (int idx = 0; idx < (importExplorerSlotTo - importExplorerSlotFrom + 1); idx++)
    {
        int instrIdx = importStartSlot + idx;
        int fileIdx = importExplorerSlotFrom + idx;

        sampleImportData[instrIdx].filePath = sourceFilePath;
        sampleImportData[instrIdx].fileName = sourceFileNameList[fileIdx];

        snprintf(srcSamplePath,
                 sizeof(srcSamplePath),
                 "%s/%s",
                 sampleImportData[instrIdx].filePath,
                 sampleImportData[instrIdx].fileName);

        if (newFileManager.isFilePtiInstrument(srcSamplePath))
        {
            sampleImportData[instrIdx].ptiFile = true;

            strInstrumentFile instrumentFileBuff;
            uint32_t instrumentFileSize = fileSystem->getFileSize(srcSamplePath);

            uint8_t loadStatus = fileTransfer.loadFileToMemory(srcSamplePath,
                                                               (uint8_t*)&instrumentFileBuff,
                                                               sizeof(strInstrumentFile),
                                                               fileWholeOnce);
            if (loadStatus == fileTransferEnd &&
                newFileManager.loadInstrumentFormFileStruct(&mtProject.instrument[instrIdx],
                                                            &instrumentFileBuff,
                                                            instrumentFileSize,
                                                            sampleImportData[instrIdx].fileOffset))
            {
                newFileManager.removeWavExtensionFromName(
                  mtProject.instrument[instrIdx].sample.file_name);
                mtProject.instrument[instrIdx].isActive = 1;

                //-------------------------------------------------------------------------------------------
                bool ptiCorrect = true;
                uint32_t instrumentSampleDataSize =
                  instrumentFileSize - sampleImportData[instrIdx].fileOffset;

                // check if contains audio data
                if (instrumentFileSize <= sampleImportData[instrIdx].fileOffset)
                {
                    ptiCorrect = false;
                }
                // instruments with length != sample_data_size exception handling
                else if (mtProject.instrument[instrIdx].sample.length == 0 ||
                         (mtProject.instrument[instrIdx].sample.length * 2 !=
                            instrumentSampleDataSize &&
                          mtProject.instrument[instrIdx].sample.length * 4 !=
                            instrumentSampleDataSize))
                {
#ifdef FILEMANAGER_DEBUGLOG
                    LOGF("Instrument (%d) has incorrect length: %d vs %d, %s\n",
                         instrIdx,
                         mtProject.instrument[instrIdx].sample.length,
                         instrumentSampleDataSize / 2,
                         srcSamplePath);
#endif
                    mtProject.instrument[instrIdx].sample.length = instrumentSampleDataSize / 2;
                }

                //-------------------------------------------------------------------------------------------

                if (ptiCorrect)
                {
                    bool isStereo = false;

                    newFileManager.isPtiFileValid(srcSamplePath,
                                                  isStereo,
                                                  sampleImportData[instrIdx].fileOffset,
                                                  mtProject.instrument[instrIdx].sample.length);

                    isStereo = isStereo && newFileManager.getSampleMode() ==
                                             FileManagerOperation::kLeftAndRight;

                    newFileManager.setInstrumentSampleAddress(
                      instrIdx, isStereo ? 2 : 1, mtProject.instrument[instrIdx].sample.length);

                    memoryToImportTotal +=
                      mtProject.instrument[instrIdx].sample.length * (isStereo ? 4 : 2);
                    continue;
                }
            }
        }
        else
        {
            sampleImportData[instrIdx].ptiFile = false;

            auto [isOpen, fileHandle] =
              fileSystem->openFile(srcSamplePath, FileSystem::FileOpenMode::kRead);
            if (isOpen)
            {
                if (readHeader(&sampleHead, fileHandle) && checkSampleValid(sampleHead))
                {
                    if (mtProject.instrument[instrIdx].isActive == 0)
                    {
                        newFileManager.setDefaultActiveInstrument(&mtProject.instrument[instrIdx]);
                    }

                    if (importIsLofi)
                    {
                        mtProject.instrument[instrIdx].tuneResample = -12;

                        // trigger resampling in convertAudioData to target sampleRate
                        // by faking source rate in inverse proportions
                        sampleHead.sampleRate =
                          (uint64_t)sampleHead.sampleRate * targetSampleRate / lofiSampleRate;
                        sampleHead.byteRate =
                          (uint64_t)sampleHead.byteRate * targetSampleRate / lofiSampleRate;
                    }

                    uint32_t fileAvailableBytes = fileSystem->getFileAvailable(fileHandle);
                    sampleImportData[instrIdx].fileOffset =
                      fileSystem->getFileSize(fileHandle) - fileAvailableBytes;

                    memoryToImportTotal += fileAvailableBytes; // load to memory size (stage 1)

                    fillImportStruct(instrIdx,
                                     sampleImportData,
                                     &sampleHead,
                                     fileAvailableBytes,
                                     sampleChannelMode,
                                     importDataFrom,
                                     importDataTo);

                    memoryToImportTotal += sampleImportData[instrIdx].destChannelCount *
                                           sampleImportData[instrIdx].destSampleCountPerChannel *
                                           2; // save memory to file size (stage 2)

                    fileSystem->closeFile(fileHandle);

                    newFileManager.copySampleNameWithoutExtension(
                      mtProject.instrument[instrIdx].sample.file_name,
                      sampleImportData[instrIdx].fileName,
                      SAMPLE_NAME_SIZE);

                    // plan 023: a sample that does not fit the free pool is converted straight to
                    // the SD card and kept as a streamed sample (pre-cache only in the pool)
                    const uint32_t channels = sampleImportData[instrIdx].destChannelCount;
                    const uint32_t frames = sampleImportData[instrIdx].destSampleCountPerChannel;
                    const uint32_t usedNow = mtProject.used_memory + importPlannedBytes;
                    const uint32_t freeNow =
                      mtProject.max_memory > usedNow ? mtProject.max_memory - usedNow : 0;
                    const sampmem::Placement placement = sampmem::decideImport(
                      frames,
                      channels,
                      playModeNeedsResident(mtProject.instrument[instrIdx].playMode),
                      freeNow);

                    if (placement == sampmem::Placement::Refused)
                    {
                        newFileManager.completeLoadedInstrumentStruct(instrIdx, false);
                        continue;
                    }

                    if (placement == sampmem::Placement::StreamedOneShot)
                    {
                        mtProject.instrument[instrIdx].playMode = playModeSingleShot;
                        mtProject.instrument[instrIdx].sample.type = mtSampleTypeWaveFile;
                    }

                    newFileManager.completeLoadedInstrumentStruct(instrIdx, true, frames);

                    if (placement == sampmem::Placement::Resident)
                    {
                        importPlannedBytes += sampmem::sampleBytes(frames, channels);
                        newFileManager.setInstrumentSampleAddress(instrIdx, channels, frames);
                    }
                    else
                    {
                        sampleImportData[instrIdx].streamImport = true;
                        importPlannedBytes += sampmem::preCacheBytes(frames, channels);
                        newFileManager.setInstrumentSampleAddress(
                          instrIdx, channels, frames, sampmem::kPreCacheFrames);
                    }

                    continue;
                }

                fileSystem->closeFile(fileHandle);
            }
        }

        newFileManager.completeLoadedInstrumentStruct(instrIdx, false);
    }
}

void SampleImport::fillImportStruct(int idx,
                                    FileManagerOperation::SampleLoadData* sampleImportData,
                                    strWavFileHeader* sampleHead,
                                    uint32_t fileWaveDataSize,
                                    SampleChannelMode channelMode,
                                    uint16_t dataFrom,
                                    uint16_t dataTo)
{
    sampleImportData[idx].sourceFormat = sampleHead->AudioFormat;
    sampleImportData[idx].sourceChannelCount = sampleHead->numChannels;
    sampleImportData[idx].sourceBitsPerSample = sampleHead->bitsPerSample;
    sampleImportData[idx].sourceSampleRate = sampleHead->sampleRate;

    sampleImportData[idx].sourceAllChannelBytesPerSample =
      (sampleHead->bitsPerSample / 8) * sampleHead->numChannels;

    sampleImportData[idx].sourceBytesPerSec =
      sampleHead->sampleRate * sampleImportData[idx].sourceAllChannelBytesPerSample;

    sampleImportData[idx].highQualitySource =
      (sampleImportData[idx].sourceBytesPerSec >= targetSampleRate * 1 * 2);

    uint32_t sourceSizeBytes = (fileWaveDataSize <= sampleHead->subchunk2Size)
                                 ? fileWaveDataSize
                                 : sampleHead->subchunk2Size;

    sampleImportData[idx].sourceTotalSamplesCountPerChannel =
      sourceSizeBytes / sampleImportData[idx].sourceAllChannelBytesPerSample;

    // import sample range implementation
    if ((dataFrom > 0 || dataTo < std::numeric_limits<uint16_t>::max()) && dataTo > dataFrom)
    {
        float start = (float)dataFrom / std::numeric_limits<uint16_t>::max();
        float end = (float)dataTo / std::numeric_limits<uint16_t>::max();

        sampleImportData[idx].fileOffset +=
          ((uint32_t)(sampleImportData[idx].sourceTotalSamplesCountPerChannel * start)) *
          sampleImportData[idx].sourceAllChannelBytesPerSample;

        sampleImportData[idx].sourceTotalSamplesCountPerChannel =
          sampleImportData[idx].sourceTotalSamplesCountPerChannel * (end - start);
    }

    // conversion is always set if sample is not mono because even correct rate/format stereo
    // also need convert to two different buffers
    // TODO: optimize later conversion if sample is stereo but has correct other parameters to
    // only split between two buffers
    sampleImportData[idx].conversion =
      sampleHead->bitsPerSample != 16 || sampleHead->numChannels != 1 ||
      sampleHead->AudioFormat != 1 || sampleHead->sampleRate != targetSampleRate;

    uint64_t samplesCount = (uint64_t)sampleImportData[idx].sourceTotalSamplesCountPerChannel *
                            targetSampleRate / sampleHead->sampleRate;

    sampleImportData[idx].destSampleCountPerChannel = (uint32_t)samplesCount;

    sampleImportData[idx].destChannelCount =
      ((channelMode == kLeftAndRight) && (sampleHead->numChannels > 1)) ? 2 : 1;
}

bool SampleImport::loadSampleDataToMemeory(int idx,
                                           FileManagerOperation::SampleLoadData* sampleImportData)
{
    if (mtProject.instrument[idx].isActive != 1)
    {
        return true;
    }

    if (sampleImportData->streamImport)
    {
        return streamConvertToWorkspace(idx, sampleImportData);
    }

    char srcSamplePath[PATCH_SIZE];
    snprintf(srcSamplePath,
             sizeof(srcSamplePath),
             "%s/%s",
             sampleImportData->filePath,
             sampleImportData->fileName);

    uint8_t loadStatus = fileTransferIdle;
    uint32_t currentSampleSamplesCount = 0;

    if (sampleImportData->ptiFile)
    {
        uint32_t offsetFromStruct =
          sampleImportData->fileOffset; // + (sizeof(strInstrumentFile) - sizeof(strInstrument));
        uint32_t sizeLimit =
          mtProject.instrument[idx].sample.length *
          ((newFileManager.isInstrumentStereo(idx) &&
            newFileManager.getSampleMode() == FileManagerOperation::kLeftAndRight)
             ? 4
             : 2);

        loadStatus = fileTransfer.loadInstrumentRawSampleDataToMemory(
          srcSamplePath,
          offsetFromStruct,
          (uint8_t*)mtProject.instrument[idx].sample.address,
          sizeLimit,
          currentSampleSamplesCount);
    }
    else
    {
        loadStatus = fileTransfer.loadSampleDataToMemory(
          sampleImportData, mtProject.instrument[idx].sample.address, targetSampleRate);
    }

    if (loadStatus == fileTransferEnd)
    {
        if (sampleImportData->ptiFile)
        {
            mtProject.used_memory += currentSampleSamplesCount * 2;
        }
        else
        {
            mtProject.used_memory += (sampleImportData->destSampleCountPerChannel * 2) *
                                     sampleImportData->destChannelCount;
        }
        return true;
    }
    else if (loadStatus == fileTransferFileNoExist || loadStatus == fileTransferFileNoValid)
    {
        newFileManager.completeLoadedInstrumentStruct(idx, false);
        return true;
    }
    else if (loadStatus >= fileTransferError)
    {
        newFileManager.completeLoadedInstrumentStruct(idx, false);
        stopOperationWithError(fmImportSamplesError);
    }

    return false;
}

//=================================================================================================
// Streamed import (plan 023): the converted sample never has to be resident. The WAV is read and
// converted in chunks and written to the workspace instrument file (left channel block, then the
// right channel block, the .pti layout); afterwards the stream is set up like for any streamed
// instrument, which loads the pre-cache into the slot's pool area.
//=================================================================================================
void SampleImport::streamConvertClose()
{
    if (streamSrc != nullptr)
    {
        fileSystem->closeFile(streamSrc);
        streamSrc = nullptr;
    }
    if (streamDst != nullptr)
    {
        fileSystem->closeFile(streamDst);
        streamDst = nullptr;
    }
}

bool SampleImport::streamConvertToWorkspace(int idx, FileManagerOperation::SampleLoadData* d)
{
    const uint32_t frames = d->destSampleCountPerChannel;
    const uint32_t channels = d->destChannelCount;
    const uint32_t headerSize = sizeof(strInstrumentFile);

    char dstPath[PATCH_SIZE];
    snprintf(dstPath, sizeof(dstPath), cWorkspaceInstrumentFileFormatNew, idx + 1);

    if (streamStep == 0)
    {
        char srcPath[PATCH_SIZE];
        snprintf(srcPath, sizeof(srcPath), "%s/%s", d->filePath, d->fileName);

        // the workspace file gets its header first; the data follows at headerSize
        newFileManager.setInstrumentStructChanged(idx);
        newFileManager.saveInstrumentStructToWorkspace(idx);

        auto [srcOpen, srcH] = fileSystem->openFile(srcPath, FileSystem::FileOpenMode::kRead);
        auto [dstOpen, dstH] = fileSystem->openFile(dstPath, FileSystem::FileOpenMode::kWriteOpen);
        if (!srcOpen || !dstOpen || frames == 0)
        {
            if (srcOpen)
            {
                fileSystem->closeFile(srcH);
            }
            if (dstOpen)
            {
                fileSystem->closeFile(dstH);
            }
            newFileManager.completeLoadedInstrumentStruct(idx, false);
            return true;
        }
        streamSrc = srcH;
        streamDst = dstH;
        streamPass = 0;
        streamSrcDone = 0;
        streamFramesDone = 0;
        fileSystem->seekFile(d->fileOffset, streamSrc);
        fileSystem->seekFile(headerSize, streamDst);
        streamStep = 1;
        return false;
    }

    // one chunk per call (keeps the main loop responsive)
    const uint32_t srcBytesPerFrame = d->sourceAllChannelBytesPerSample;
    const uint32_t srcTotalBytes = srcBytesPerFrame * d->sourceTotalSamplesCountPerChannel;
    int16_t* bufL = (int16_t*)externalRamFileManagerBuffL;
    int16_t* bufR = bufL + kStreamOutHalfFrames;

    uint32_t chunkFrames = kStreamSrcChunkBytes / srcBytesPerFrame;
    uint32_t maxByOut =
      (uint32_t)((uint64_t)kStreamOutHalfFrames * d->sourceSampleRate / targetSampleRate);
    if (chunkFrames > maxByOut)
    {
        chunkFrames = maxByOut;
    }
    if (chunkFrames == 0)
    {
        chunkFrames = 1;
    }
    uint32_t want = chunkFrames * srcBytesPerFrame;
    if (want > srcTotalBytes - streamSrcDone)
    {
        want = srcTotalBytes - streamSrcDone;
    }

    int32_t got = 0;
    if (want >= srcBytesPerFrame)
    {
        got = fileSystem->readFile(externalRamFileManagerSrcBuff, want, streamSrc);
    }

    bool passDone = false;
    if (got < (int32_t)srcBytesPerFrame)
    {
        passDone = true; // end of source data (or short file): the rest is padded with silence
    }
    else
    {
        got -= got % srcBytesPerFrame;
        const int16_t* out = nullptr;
        uint32_t outFrames = 0;

        if (d->conversion)
        {
            ConversionConfig cfg;
            cfg.inputData = externalRamFileManagerSrcBuff;
            cfg.inputSize = got;
            cfg.outPtrL = bufL;
            cfg.outPtrR = (channels == 2) ? bufR : nullptr;
            cfg.sourceChannelCount = d->sourceChannelCount;
            cfg.sourceBitsPerSample = d->sourceBitsPerSample;
            cfg.sourceSampleRate = d->sourceSampleRate;
            cfg.sourceFormat = d->sourceFormat;
            cfg.destSampleRate = targetSampleRate;
            outFrames = convertAudioData(cfg);
            out = (streamPass == 0) ? bufL : bufR;
        }
        else // 16 bit mono at the target rate: no conversion
        {
            outFrames = got / 2;
            out = (const int16_t*)externalRamFileManagerSrcBuff;
        }

        if (outFrames > frames - streamFramesDone)
        {
            outFrames = frames - streamFramesDone;
        }
        if (outFrames > 0)
        {
            int32_t wr = fileSystem->writeFile(out, outFrames * 2, streamDst);
            if (wr != (int32_t)(outFrames * 2))
            {
                streamConvertClose();
                streamStep = 0;
                newFileManager.completeLoadedInstrumentStruct(idx, false);
                stopOperationWithError(fmImportSamplesError);
                return true;
            }
            streamFramesDone += outFrames;
            memoryToImportDone += outFrames * 2;
        }
        streamSrcDone += got;
        if (streamPass == 0)
        {
            memoryToImportDone += got;
        }
        if (streamFramesDone >= frames || streamSrcDone >= srcTotalBytes)
        {
            passDone = true;
        }
    }

    if (!passDone)
    {
        return false;
    }

    // pad the rest of this channel with silence (rounding of the resampled length)
    if (streamFramesDone < frames)
    {
        memset(externalRamFileManagerBuffL, 0, kStreamOutHalfFrames * 2);
        while (streamFramesDone < frames)
        {
            uint32_t n = frames - streamFramesDone;
            if (n > kStreamOutHalfFrames)
            {
                n = kStreamOutHalfFrames;
            }
            if (fileSystem->writeFile(externalRamFileManagerBuffL, n * 2, streamDst) !=
                (int32_t)(n * 2))
            {
                streamConvertClose();
                streamStep = 0;
                newFileManager.completeLoadedInstrumentStruct(idx, false);
                stopOperationWithError(fmImportSamplesError);
                return true;
            }
            streamFramesDone += n;
        }
    }

    if ((uint32_t)streamPass + 1 < channels)
    {
        // second channel: read the source again from its start
        streamPass++;
        streamSrcDone = 0;
        streamFramesDone = 0;
        fileSystem->seekFile(d->fileOffset, streamSrc);
        return false;
    }

    fileSystem->endFile(streamDst); // drop any stale tail of an older, longer file
    streamConvertClose();
    streamStep = 0;

    if (!voiceStreamManager.setupInstrumentStream(idx, dstPath, headerSize, frames, channels == 2))
    {
        newFileManager.completeLoadedInstrumentStruct(idx, false);
        return true;
    }

    mtProject.used_memory += sampmem::preCacheBytes(frames, channels);
    return true;
}
