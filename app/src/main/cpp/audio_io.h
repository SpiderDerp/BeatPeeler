#ifndef ECE420_AUDIO_IO_H
#define ECE420_AUDIO_IO_H

#include <string>
#include <vector>
#include <cstdint>

/**
 * Simple WAV file I/O for 16-bit mono PCM audio
 */

struct WavHeader {
    // "RIFF" chunk
    uint32_t ChunkID;          // Must be 0x46464952 ('RIFF')
    uint32_t ChunkSize;        // File size - 8
    uint32_t Format;           // Must be 0x45564157 ('WAVE')

    // "fmt " chunk
    uint32_t Subchunk1ID;      // Must be 0x20746d66 ('fmt ')
    uint32_t Subchunk1Size;    // 16 for PCM
    uint16_t AudioFormat;      // 1 for PCM
    uint16_t NumChannels;      // 1 for mono
    uint32_t SampleRate;       // e.g., 44100
    uint32_t ByteRate;         // SampleRate * NumChannels * BitsPerSample/8
    uint16_t BlockAlign;       // NumChannels * BitsPerSample/8
    uint16_t BitsPerSample;    // 16 for 16-bit

    // "data" chunk
    uint32_t Subchunk2ID;      // Must be 0x61746164 ('data')
    uint32_t Subchunk2Size;    // Number of bytes in audio data
};

/**
 * Read a 16-bit mono PCM WAV file
 * @param filename Path to WAV file
 * @param audioData Output vector of samples (as float, normalized to [-1, 1])
 * @param sampleRate Output sample rate in Hz
 * @return true on success
 */
bool readWavFile(const std::string& filename, std::vector<float>& audioData, uint32_t& sampleRate);

/**
 * Write a 16-bit mono PCM WAV file
 * @param filename Path to WAV file to write
 * @param audioData Vector of samples (as float, expected to be in [-1, 1])
 * @param sampleRate Sample rate in Hz
 * @return true on success
 */
bool writeWavFile(const std::string& filename, const std::vector<float>& audioData, uint32_t sampleRate);

#endif // ECE420_AUDIO_IO_H
