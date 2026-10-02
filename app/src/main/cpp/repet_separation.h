#ifndef ECE420_REPET_SEPARATION_H
#define ECE420_REPET_SEPARATION_H

#include <vector>
#include <complex>
#include <cstdint>
#include <functional>

/**
 * REPET (REpeating Pattern Extraction Technique) algorithm for background/foreground separation
 */

// Progress callback type: (progress_percent, message)
using ProgressCallback = std::function<void(int, const char*)>;

struct RepetParams {
    int frameSize;              // STFT frame size (default 2048)
    int windowSec;              // Window length in seconds for processing (default 10)
    float minPeriodSec;         // Minimum period to search for in seconds (default 0.5)
    float maxPeriodSec;         // Maximum period to search for in seconds (default 4.0)
    int periodFrames;           // -1 for auto-detect, otherwise fixed period
    float highpassCutoff;       // High-pass filter cutoff in Hz (default 100)
    ProgressCallback progressCallback;  // Optional progress callback
};

/**
 * Apply REPET algorithm to separate repeating background from non-repeating foreground
 * 
 * @param audioData Input audio signal (mono, float samples in [-1, 1])
 * @param sampleRate Sample rate in Hz
 * @param background Output: separated background audio
 * @param foreground Output: separated foreground audio
 * @param params REPET algorithm parameters
 * @return true on success
 */
bool repetSeparation(
    const std::vector<float>& audioData,
    uint32_t sampleRate,
    std::vector<float>& background,
    std::vector<float>& foreground,
    const RepetParams& params = RepetParams{2048, 10, 0.5f, 4.0f, -1, 100.0f, nullptr}
);

/**
 * Apply REPET with windowed processing for long audio files
 * Processes audio in overlapping windows with Hann window fade for smooth transitions
 * 
 * @param audioData Input audio signal (mono, float samples in [-1, 1])
 * @param sampleRate Sample rate in Hz
 * @param background Output: separated background audio
 * @param foreground Output: separated foreground audio
 * @param windowSec Window length in seconds (default 10)
 * @param overlap Overlap fraction between windows, 0-1 (default 0.5 = 50%)
 * @param params Additional REPET parameters (can override windowSec)
 * @return true on success
 */
bool repetSeparationWindowed(
    const std::vector<float>& audioData,
    uint32_t sampleRate,
    std::vector<float>& background,
    std::vector<float>& foreground,
    float windowSec = 10.0f,
    float overlap = 0.5f,
    const RepetParams& params = RepetParams{2048, 10, 0.5f, 4.0f, -1, 100.0f, nullptr}
);

#endif // ECE420_REPET_SEPARATION_H
