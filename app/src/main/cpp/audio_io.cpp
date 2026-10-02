#include "audio_io.h"
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <unistd.h>
#include "debug_utils.h"

bool readWavFile(const std::string& filename, std::vector<float>& audioData, uint32_t& sampleRate) {
    FILE* file = fopen(filename.c_str(), "rb");
    if (!file) {
        LOGE("Unable to open wav file: %s", filename.c_str());
        return false;
    }

    // Read RIFF header
    uint32_t chunkID;
    if (fread(&chunkID, sizeof(uint32_t), 1, file) != 1) {
        LOGE("Failed to read RIFF chunk ID");
        fclose(file);
        return false;
    }
    if (chunkID != 0x46464952) {  // "RIFF"
        LOGE("Invalid WAV format: missing RIFF signature");
        fclose(file);
        return false;
    }

    uint32_t chunkSize;
    if (fread(&chunkSize, sizeof(uint32_t), 1, file) != 1) {
        LOGE("Failed to read RIFF chunk size");
        fclose(file);
        return false;
    }

    uint32_t format;
    if (fread(&format, sizeof(uint32_t), 1, file) != 1) {
        LOGE("Failed to read WAVE format");
        fclose(file);
        return false;
    }
    if (format != 0x45564157) {  // "WAVE"
        LOGE("Invalid WAV format: missing WAVE signature");
        fclose(file);
        return false;
    }

    // Initialize format info
    uint16_t audioFormat = 0;
    uint16_t numChannels = 0;
    uint32_t subchunk2Size = 0;
    bool foundFmt = false;
    bool foundData = false;
    long dataOffset = 0;

    // Read chunks until we find fmt and data
    while (!foundFmt || !foundData) {
        uint32_t subchunkID;
        uint32_t subchunkSize;

        if (fread(&subchunkID, sizeof(uint32_t), 1, file) != 1) {
            LOGE("Failed to read subchunk ID");
            fclose(file);
            return false;
        }
        if (fread(&subchunkSize, sizeof(uint32_t), 1, file) != 1) {
            LOGE("Failed to read subchunk size");
            fclose(file);
            return false;
        }

        if (subchunkID == 0x20746d66) {  // "fmt "
            if (subchunkSize < 16) {
                LOGE("Invalid fmt chunk size: %u", subchunkSize);
                fclose(file);
                return false;
            }

            if (fread(&audioFormat, sizeof(uint16_t), 1, file) != 1) {
                LOGE("Failed to read audio format");
                fclose(file);
                return false;
            }
            if (audioFormat != 1) {
                LOGE("Unsupported audio format: only PCM (format=1) is supported, got %u", audioFormat);
                fclose(file);
                return false;
            }

            if (fread(&numChannels, sizeof(uint16_t), 1, file) != 1) {
                LOGE("Failed to read number of channels");
                fclose(file);
                return false;
            }
            if (numChannels != 1 && numChannels != 2) {
                LOGE("Unsupported number of channels: only mono (1) and stereo (2) are supported, got %u", numChannels);
                fclose(file);
                return false;
            }

            if (fread(&sampleRate, sizeof(uint32_t), 1, file) != 1) {
                LOGE("Failed to read sample rate");
                fclose(file);
                return false;
            }

            uint32_t byteRate;
            uint16_t blockAlign;
            uint16_t bitsPerSample;

            if (fread(&byteRate, sizeof(uint32_t), 1, file) != 1 ||
                fread(&blockAlign, sizeof(uint16_t), 1, file) != 1 ||
                fread(&bitsPerSample, sizeof(uint16_t), 1, file) != 1) {
                LOGE("Failed to read format details");
                fclose(file);
                return false;
            }

            if (bitsPerSample != 16) {
                LOGE("Unsupported bits per sample: only 16-bit is supported, got %u", bitsPerSample);
                fclose(file);
                return false;
            }

            // Skip any remaining bytes in fmt chunk
            uint32_t formatBytesRead = 16;
            if (subchunkSize > formatBytesRead) {
                fseek(file, subchunkSize - formatBytesRead, SEEK_CUR);
            }

            foundFmt = true;
        } else if (subchunkID == 0x61746164) {  // "data"
            subchunk2Size = subchunkSize;
            dataOffset = ftell(file);
            foundData = true;
            break;  // Stop here, we'll read the data later
        } else {
            // Skip unknown chunk
            LOGD("Skipping unknown chunk: 0x%x, size: %u", subchunkID, subchunkSize);
            fseek(file, subchunkSize, SEEK_CUR);
        }
    }

    if (!foundFmt) {
        LOGE("Failed to find fmt chunk");
        fclose(file);
        return false;
    }
    if (!foundData) {
        LOGE("Failed to find data chunk");
        fclose(file);
        return false;
    }

    // Seek to data and read audio
    if (fseek(file, dataOffset, SEEK_SET) != 0) {
        LOGE("Failed to seek to audio data");
        fclose(file);
        return false;
    }

    uint32_t numSamplesPerChannel = subchunk2Size / (sizeof(int16_t) * numChannels);
    uint32_t totalSamples = subchunk2Size / sizeof(int16_t);

    LOGD("Reading WAV: %u samples per channel, %u Hz, %u channels", numSamplesPerChannel, sampleRate, numChannels);

    std::vector<int16_t> rawData(totalSamples);
    if (fread(rawData.data(), sizeof(int16_t), totalSamples, file) != totalSamples) {
        LOGE("Failed to read audio data from WAV file");
        fclose(file);
        return false;
    }
    fclose(file);

    // Convert to float [-1, 1] and mix stereo to mono if needed
    audioData.clear();
    audioData.reserve(numSamplesPerChannel);
    if (numChannels == 1) {
        // Mono: direct conversion
        for (int16_t sample : rawData) {
            audioData.push_back(static_cast<float>(sample) / 32768.0f);
        }
    } else {
        // Stereo: mix channels by averaging
        for (uint32_t i = 0; i < numSamplesPerChannel; i++) {
            int16_t leftSample = rawData[i * 2];
            int16_t rightSample = rawData[i * 2 + 1];
            float mixedSample = (static_cast<float>(leftSample) + static_cast<float>(rightSample)) / (2.0f * 32768.0f);
            audioData.push_back(mixedSample);
        }
    }

    LOGD("Successfully read WAV file: %u samples @ %u Hz", (uint32_t)audioData.size(), sampleRate);
    return true;
}

bool writeWavFile(const std::string& filename, const std::vector<float>& audioData, uint32_t sampleRate) {
    FILE* file = fopen(filename.c_str(), "wb");
    if (!file) {
        LOGE("Unable to create wav file: %s", filename.c_str());
        return false;
    }

    uint32_t numSamples = audioData.size();
    uint32_t byteRate = sampleRate * 1 * 2;  // 1 channel, 16-bit
    uint32_t subchunk2Size = numSamples * 2;

    // Build WAV header
    WavHeader header;
    header.ChunkID = 0x46464952;               // "RIFF"
    header.ChunkSize = 36 + subchunk2Size;     // File size - 8
    header.Format = 0x45564157;                // "WAVE"
    header.Subchunk1ID = 0x20746d66;           // "fmt "
    header.Subchunk1Size = 16;                 // 16 for PCM
    header.AudioFormat = 1;                    // 1 for PCM
    header.NumChannels = 1;                    // Mono
    header.SampleRate = sampleRate;
    header.ByteRate = byteRate;
    header.BlockAlign = 2;                     // 1 channel * 16-bit / 8
    header.BitsPerSample = 16;
    header.Subchunk2ID = 0x61746164;           // "data"
    header.Subchunk2Size = subchunk2Size;

    // Write header
    if (fwrite(&header, sizeof(WavHeader), 1, file) != 1) {
        LOGE("Failed to write WAV header");
        fclose(file);
        return false;
    }

    // Convert float to int16 and write
    std::vector<int16_t> rawData;
    rawData.reserve(numSamples);
    for (float sample : audioData) {
        // Clamp to [-1, 1] and convert to int16
        float clamped = std::max(-1.0f, std::min(1.0f, sample));
        int16_t value = static_cast<int16_t>(clamped * 32767.0f);
        rawData.push_back(value);
    }

    if (fwrite(rawData.data(), sizeof(int16_t), numSamples, file) != numSamples) {
        LOGE("Failed to write audio data to WAV file");
        fclose(file);
        return false;
    }
    
    // Ensure data is written to disk
    fflush(file);
    fsync(fileno(file));
    fclose(file);

    LOGD("Wrote WAV file: %u samples @ %u Hz to %s", numSamples, sampleRate, filename.c_str());
    return true;
}
