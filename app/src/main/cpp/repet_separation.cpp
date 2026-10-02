#include "repet_separation.h"
#include "kiss_fft/kiss_fft.h"
#include "debug_utils.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <cstring>
#include <string>
#include <thread>
#include <atomic>

namespace {

int getWorkerCount(int tasks) {
    if (tasks <= 1) {
        return 1;
    }

    unsigned int hw = std::thread::hardware_concurrency();
    int workers = (hw == 0) ? 4 : static_cast<int>(hw);
    workers = std::max(1, std::min(workers, tasks));
    return workers;
}

template <typename Func>
void parallelFor(int begin, int end, Func fn) {
    int total = end - begin;
    if (total <= 0) {
        return;
    }

    int workers = getWorkerCount(total);
    if (workers == 1) {
        for (int i = begin; i < end; ++i) {
            fn(i);
        }
        return;
    }

    // dynamic scheduling keeps workers balanced for uneven task costs.
    std::atomic<int> next(begin);
    std::vector<std::thread> threads;
    threads.reserve(workers - 1);

    auto workerFn = [&]() {
        while (true) {
            int idx = next.fetch_add(1);
            if (idx >= end) {
                break;
            }
            fn(idx);
        }
    };

    for (int t = 0; t < workers - 1; ++t) {
        threads.emplace_back(workerFn);
    }
    workerFn();

    for (auto& t : threads) {
        t.join();
    }
}

}  // namespace


/*
 * High-Pass filter
 */
void highpassFilter(std::vector<float>& data, float cutoff, uint32_t fs) {
    if (data.empty()) return;
    
    //must use double precision for low-cutoff IIR stability
    double wc = 2.0 * 3.141592653589793 * cutoff / fs;
    double alpha = std::sin(wc) / (2.0 * 0.7071067811865475);  // Q = 0.707
    
    //correct High-Pass coefficients
    double a0 = 1.0 + alpha;
    double b0 = (1.0 + std::cos(wc)) / 2.0 / a0;
    double b1 = -(1.0 + std::cos(wc)) / a0;
    double b2 = (1.0 + std::cos(wc)) / 2.0 / a0;
    double a1 = -2.0 * std::cos(wc) / a0;
    double a2 = (1.0 - alpha) / a0;
    
    std::vector<double> filtered(data.size(), 0.0);
    
    //forward pass
    filtered[0] = b0 * data[0];
    if (data.size() > 1) {
        filtered[1] = b0 * data[1] + b1 * data[0] - a1 * filtered[0];
    }
    for (size_t i = 2; i < data.size(); i++) {
        filtered[i] = b0 * data[i] + b1 * data[i-1] + b2 * data[i-2]
                    - a1 * filtered[i-1] - a2 * filtered[i-2];
    }
    
    //backward pass (Zero-phase filtering)
    std::vector<double> result(data.size(), 0.0);
    result[data.size()-1] = filtered[data.size()-1];
    if (data.size() > 1) {
        result[data.size()-2] = b0 * filtered[data.size()-2] + b1 * filtered[data.size()-1]
                              - a1 * result[data.size()-1];
    }
    for (int i = (int)data.size() - 3; i >= 0; i--) {
        result[i] = b0 * filtered[i] + b1 * filtered[i+1] + b2 * filtered[i+2]
                  - a1 * result[i+1] - a2 * result[i+2];
    }
    
    //write back to float array
    for (size_t i = 0; i < data.size(); i++) {
        data[i] = static_cast<float>(result[i]);
    }
}

/*
 * Apply Hamming window
 */
void hammingWindow(std::vector<float>& window, int size) {
    window.resize(size);
    for (int i = 0; i < size; i++) {
        window[i] = 0.54f - 0.46f * std::cos(2.0f * 3.14159265f * i / (size - 1));
    }
}

/*
 * Compute median of a 2D vector slice 
 */
float computeMedian(const std::vector<float>& values) {
    if (values.empty()) return 0.0f;
    
    std::vector<float> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    
    size_t n = sorted.size();
    if (n % 2 == 0) {
        return (sorted[n/2 - 1] + sorted[n/2]) / 2.0f;
    } else {
        return sorted[n/2];
    }
}

/*
 * 2D Median Filter (Non-separable) matching scipy.ndimage.median_filter
 */
void applyMedianFilter2D(std::vector<std::vector<float>>& matrix, 
                         int kernelTimeSize, int kernelFreqSize) {
    if (matrix.empty() || matrix[0].empty()) return;
    
    int numFrames = matrix.size();
    int numFreqBins = matrix[0].size();
    std::vector<std::vector<float>> result = matrix;
    
    int halfTime = kernelTimeSize / 2;
    int halfFreq = kernelFreqSize / 2;
    
    std::vector<float> window;
    window.reserve(kernelTimeSize * kernelFreqSize);
    
    for (int t = 0; t < numFrames; t++) {
        for (int f = 0; f < numFreqBins; f++) {
            window.clear();
            
            for (int dt = -halfTime; dt <= halfTime; dt++) {
                for (int df = -halfFreq; df <= halfFreq; df++) {
                    int nt = t + dt;
                    int nf = f + df;
                    
                    //reflect boundary condition (matches Python's default behavior)
                    if (nt < 0) nt = -nt;
                    else if (nt >= numFrames) nt = 2 * numFrames - 2 - nt;
                    
                    if (nf < 0) nf = -nf;
                    else if (nf >= numFreqBins) nf = 2 * numFreqBins - 2 - nf;
                    
                    //clamp as a fallback
                    nt = std::max(0, std::min(numFrames - 1, nt));
                    nf = std::max(0, std::min(numFreqBins - 1, nf));
                    
                    window.push_back(matrix[nt][nf]);
                }
            }
            
            //compute median using nth_element (much faster than full sort)
            int n = window.size();
            std::nth_element(window.begin(), window.begin() + n / 2, window.end());
            result[t][f] = window[n / 2];
        }
    }
    
    matrix = result;
}

/*
 * Main REPET algorithm
 */
bool repetSeparation(
    const std::vector<float>& audioData,
    uint32_t sampleRate,
    std::vector<float>& background,
    std::vector<float>& foreground,
    const RepetParams& params) {

    if (audioData.empty()) {
        LOGE("Empty audio data");
        return false;
    }

    auto reportProgress = [&](int progress, const char* message) {
        if (params.progressCallback) {
            params.progressCallback(progress, message);
        }
        LOGD("REPET Progress: %d%% - %s", progress, message);
    };

    int frameSize = params.frameSize;
    int hopSize = frameSize / 2;
    int fftSize = frameSize * 2;

    if ((int)audioData.size() < frameSize) {
        LOGE("Input audio shorter than frame size");
        return false;
    }
    
    //STAGE 1: Compute STFT
    int numFrames = (audioData.size() - frameSize) / hopSize + 1;
    
    LOGD("REPET: frameSize=%d, hopSize=%d, fftSize=%d, numFrames=%d, fs=%u",
         frameSize, hopSize, fftSize, numFrames, sampleRate);
    
    reportProgress(5, "Computing STFT...");
    
    //V: magnitude spectrogram
    std::vector<std::vector<float>> V(numFrames, std::vector<float>(fftSize/2, 0.0f));
    //STFT_complex: complex STFT
    std::vector<std::vector<std::complex<float>>> STFT_complex(numFrames, 
                                                              std::vector<std::complex<float>>(fftSize/2, 0.0f));
    
    //create hamming window
    std::vector<float> window;
    hammingWindow(window, frameSize);
    
    // allocate one forward fft plan and reuse it for all frames.
    kiss_fft_cfg stftCfg = kiss_fft_alloc(fftSize, 0, nullptr, nullptr);
    if (!stftCfg) {
        LOGE("Failed to allocate STFT FFT plan");
        return false;
    }

    std::atomic<int> stftDone(0);
    int stftProgressStride = std::max(1, numFrames / 20);
    // each frame is independent, so this loop parallelizes safely.
    parallelFor(0, numFrames, [&](int i) {
        int start = i * hopSize;

        std::vector<float> zeroPadded(fftSize, 0.0f);
        for (int j = 0; j < frameSize; j++) {
            zeroPadded[j] = audioData[start + j] * window[j];
        }

        std::vector<kiss_fft_cpx> in(fftSize), out(fftSize);
        for (int j = 0; j < fftSize; j++) {
            in[j].r = zeroPadded[j];
            in[j].i = 0.0f;
        }

        kiss_fft(stftCfg, in.data(), out.data());

        for (int j = 0; j < fftSize / 2; j++) {
            STFT_complex[i][j] = std::complex<float>(out[j].r, out[j].i);
            V[i][j] = std::sqrt(out[j].r * out[j].r + out[j].i * out[j].i);
        }

        int done = stftDone.fetch_add(1) + 1;
        if (done % stftProgressStride == 0 || done == numFrames) {
            int progress = 5 + (int)(15.0f * done / std::max(numFrames, 1));
            reportProgress(progress, "Computing STFT...");
        }
    });
    free(stftCfg);
    
    reportProgress(20, "Finding repeating period...");
    
    //STAGE 2: Find repeating period
    int period = params.periodFrames;
    
    if (period <= 0) {
        //compute beat spectrum (autocorrelation of V^2) using FFT for efficiency
        std::vector<float> b(numFrames, 0.0f);
        
        //limit the search space for faster computation
        int maxSearchFrames = (numFrames > 2000) ? 2000 : numFrames;
        
        int fft_size_autocorr = 1;
        while (fft_size_autocorr < 2 * numFrames) {
            fft_size_autocorr *= 2;
        }

        // reuse shared autocorrelation plans instead of reallocating per bin.
        kiss_fft_cfg cfg_fwd = kiss_fft_alloc(fft_size_autocorr, 0, nullptr, nullptr);
        kiss_fft_cfg cfg_inv = kiss_fft_alloc(fft_size_autocorr, 1, nullptr, nullptr);
        if (!cfg_fwd || !cfg_inv) {
            if (cfg_fwd) free(cfg_fwd);
            if (cfg_inv) free(cfg_inv);
            LOGE("Failed to allocate autocorrelation FFT plans");
            return false;
        }

        int freqBins = fftSize / 2;
        int lagLimit = std::min(maxSearchFrames, numFrames);
        int workerCount = getWorkerCount(freqBins);
        // each worker accumulates locally to avoid contention on b.
        std::vector<std::vector<float>> partialB(workerCount, std::vector<float>(numFrames, 0.0f));

        std::atomic<int> nextFreq(0);
        std::atomic<int> freqDone(0);
        int freqProgressStride = std::max(1, freqBins / 20);
        std::vector<std::thread> workers;
        workers.reserve(workerCount);

        for (int workerIdx = 0; workerIdx < workerCount; ++workerIdx) {
            workers.emplace_back([&, workerIdx]() {
                std::vector<float> padded(fft_size_autocorr, 0.0f);
                std::vector<kiss_fft_cpx> in_fwd(fft_size_autocorr), out_fwd(fft_size_autocorr);
                std::vector<kiss_fft_cpx> power_spectrum(fft_size_autocorr);
                std::vector<kiss_fft_cpx> autocorr(fft_size_autocorr);

                while (true) {
                    int freqBin = nextFreq.fetch_add(1);
                    if (freqBin >= freqBins) {
                        break;
                    }

                    std::fill(padded.begin(), padded.end(), 0.0f);
                    for (int t = 0; t < numFrames; t++) {
                        float v = V[t][freqBin];
                        padded[t] = v * v;
                    }

                    for (int j = 0; j < fft_size_autocorr; j++) {
                        in_fwd[j].r = padded[j];
                        in_fwd[j].i = 0.0f;
                    }

                    kiss_fft(cfg_fwd, in_fwd.data(), out_fwd.data());

                    for (int j = 0; j < fft_size_autocorr; j++) {
                        float mag_sq = out_fwd[j].r * out_fwd[j].r + out_fwd[j].i * out_fwd[j].i;
                        power_spectrum[j].r = mag_sq;
                        power_spectrum[j].i = 0.0f;
                    }

                    kiss_fft(cfg_inv, power_spectrum.data(), autocorr.data());

                    std::vector<float>& localB = partialB[workerIdx];
                    for (int lag = 0; lag < lagLimit; lag++) {
                        localB[lag] += autocorr[lag].r / fft_size_autocorr;
                    }

                    int done = freqDone.fetch_add(1) + 1;
                    if (done % freqProgressStride == 0 || done == freqBins) {
                        int progress = 20 + (int)(15.0f * done / std::max(freqBins, 1));
                        reportProgress(progress, "Processing frequency bins...");
                    }
                }
            });
        }

        for (auto& worker : workers) {
            worker.join();
        }

        // reduce per-thread partial sums into the final beat spectrum.
        for (int w = 0; w < workerCount; ++w) {
            for (int lag = 0; lag < lagLimit; ++lag) {
                b[lag] += partialB[w][lag];
            }
        }

        free(cfg_fwd);
        free(cfg_inv);
        
        //normalize
        if (b[0] > 0.0f) {
            for (int i = 0; i < maxSearchFrames; i++) {
                b[i] /= b[0];
            }
        }
        
        //search for best period
        int l = (int)(maxSearchFrames * 0.75f);
        int searchLimit = l / 3;
        std::vector<float> J(searchLimit + 1, 0.0f);
        
        float frameRate = (float)sampleRate / hopSize;
        int minLag = std::max(2, (int)(params.minPeriodSec * frameRate));
        int maxLag = std::min(searchLimit, (int)(params.maxPeriodSec * frameRate));
        
        LOGD("REPET: frameRate=%.1f, minLag=%d, maxLag=%d, maxSearchFrames=%d", frameRate, minLag, maxLag, maxSearchFrames);
        
        reportProgress(35, "Searching for repeating period...");
        
        const int delta = 2;
        
        for (int j = minLag; j < maxLag && j <= searchLimit; j++) {
            int Delta = std::max(2, (int)std::floor(j * 0.1f));
            float accumulatedEnergy = 0.0f;
            int count = 0;
            
            for (int mult = j; mult < l; mult += j) {
                int startWin = std::max(0, mult - Delta);
                int endWin = std::min(l, mult + Delta + 1);
                
                if (startWin >= (int)b.size() || endWin > (int)b.size()) continue;
                
                float maxVal = b[startWin];
                int maxIdx = startWin;
                for (int idx = startWin; idx < endWin; idx++) {
                    if (b[idx] > maxVal) {
                        maxVal = b[idx];
                        maxIdx = idx;
                    }
                }
                
                if (maxIdx >= mult - delta && maxIdx <= mult + delta) {
                    float mean = 0.0f;
                    for (int idx = startWin; idx < endWin; idx++) {
                        mean += b[idx];
                    }
                    mean /= (endWin - startWin);
                    accumulatedEnergy += (b[maxIdx] - mean);
                    count++;
                }
            }
            
            if (count > 0) {
                J[j] = accumulatedEnergy / count;
            }
        }
        
        //find best period
        float maxJ = 0.0f;
        period = minLag;
        for (int j = minLag; j < maxLag && j <= searchLimit; j++) {
            if (J[j] > maxJ) {
                maxJ = J[j];
                period = j;
            }
        }
    }
    
    reportProgress(50, "Creating pattern model...");
    
    LOGD("REPET: Detected period=%d frames (%.2f sec)", period, period * hopSize / (float)sampleRate);
    
    //STAGE 3: Create repeating pattern model
    //first, segment V into chunks of length 'period'
    std::vector<std::vector<std::vector<float>>> segments;
    
    for (int start = 0; start + period <= numFrames; start += period) {
        std::vector<std::vector<float>> segment(period);
        for (int i = 0; i < period; i++) {
            segment[i] = V[start + i];
        }
        segments.push_back(segment);
    }
    
    //safety check: if the period is too long to get 3 segments, abort REPET
    if (segments.size() < 3) {
        LOGW("REPET: Too few segments (%lu), returning original audio", segments.size());
        background = audioData;
        foreground.assign(audioData.size(), 0.0f);
        return true;
    }
    
    //now, compute the median across all segments to create the repeating pattern S
    //must be a 2D matrix of shape (period, fftSize/2) to match Python
    std::vector<std::vector<float>> S(period, std::vector<float>(fftSize/2, 0.0f));
    
    int freqBins = fftSize / 2;
    std::atomic<int> modelDone(0);
    int modelProgressStride = std::max(1, freqBins / 20);
    // median per frequency bin is independent, so this stage parallelizes well.
    parallelFor(0, freqBins, [&](int freqBin) {
        for (int i = 0; i < period; i++) {
            std::vector<float> values;
            values.reserve(segments.size());
            for (const auto& segment : segments) {
                //collect the same relative frame 'i' across all segments
                values.push_back(segment[i][freqBin]);
            }
            S[i][freqBin] = computeMedian(values);
        }

        int done = modelDone.fetch_add(1) + 1;
        if (done % modelProgressStride == 0 || done == freqBins) {
            int progress = 50 + (int)(20.0f * done / std::max(freqBins, 1));
            reportProgress(progress, "Creating pattern model...");
        }
    });
    
    //STAGE 4: Create background model and mask
    std::vector<std::vector<float>> W(numFrames, std::vector<float>(fftSize/2, 0.0f));
    
    for (int start = 0; start + period <= numFrames; start += period) {
        for (int i = 0; i < period && start + i < numFrames; i++) {
            for (int j = 0; j < fftSize/2; j++) {
                //compare against S[i][j], preserving the temporal pattern
                W[start + i][j] = std::min(S[i][j], V[start + i][j]);
            }
        }
    }

    //create Wiener mask
    std::vector<std::vector<float>> M(numFrames, std::vector<float>(fftSize/2, 0.0f));
    float beta = 1.5f;
    const float epsilon = 1e-10f;
    const float threshold = 0.3f;
    
    for (int i = 0; i < numFrames; i++) {
        for (int j = 0; j < fftSize/2; j++) {
            float W_b = std::pow(W[i][j], beta);
            float V_b = std::pow(V[i][j], beta) + epsilon;
            float mask = W_b / V_b;
            // Clip between 0 and 1, but do NOT threshold yet
            M[i][j] = std::max(0.0f, std::min(1.0f, mask));
        }
    }
    
    //apply 2D median filter first to match Python
    reportProgress(78, "Smoothing mask...");
    applyMedianFilter2D(M, 3, 5);
    
    //apply hard threshold after median filtering
    for (int i = 0; i < numFrames; i++) {
        for (int j = 0; j < fftSize/2; j++) {
            if (M[i][j] <= threshold) {
                M[i][j] = 0.0f;
            }
        }
    }
    
    reportProgress(90, "Reconstructing audio...");
    
    //STAGE 5: Apply mask and reconstruct
    std::vector<std::vector<std::complex<float>>> background_STFT(numFrames, 
                                                                 std::vector<std::complex<float>>(fftSize/2, 0.0f));
    std::vector<std::vector<std::complex<float>>> foreground_STFT(numFrames, 
                                                                 std::vector<std::complex<float>>(fftSize/2, 0.0f));
    
    for (int i = 0; i < numFrames; i++) {
        for (int j = 0; j < fftSize/2; j++) {
            background_STFT[i][j] = M[i][j] * STFT_complex[i][j];
            foreground_STFT[i][j] = (1.0f - M[i][j]) * STFT_complex[i][j];
        }
    }
    
    //inverse STFT
    auto istft = [&](const std::vector<std::vector<std::complex<float>>>& stft_matrix) -> std::vector<float> {
        int numF = stft_matrix.size();
        int signalLength = (numF - 1) * hopSize + frameSize;
        std::vector<float> reconstructed(signalLength, 0.0f);
        std::vector<float> windowSum(signalLength, 0.0f);
        
        // allocate one inverse fft plan per istft call and reuse it across frames.
        kiss_fft_cfg ifftCfg = kiss_fft_alloc(fftSize, 1, nullptr, nullptr);
        if (!ifftCfg) {
            LOGE("Failed to allocate inverse FFT plan");
            return std::vector<float>();
        }

        for (int i = 0; i < numF; i++) {
            //reconstruct full FFT
            std::vector<kiss_fft_cpx> in(fftSize), out(fftSize);
            
            //zero initialize memory to prevent garbage data
            memset(in.data(), 0, sizeof(kiss_fft_cpx) * fftSize);
            
            //DC Bin is purely real
            in[0].r = stft_matrix[i][0].real();
            in[0].i = 0.0f;
            
            //positive frequencies and mirror for negative frequencies
            for (int j = 1; j < fftSize/2; j++) {
                in[j].r = stft_matrix[i][j].real();
                in[j].i = stft_matrix[i][j].imag();
                
                //complex conjugate for symmetric negative frequencies
                in[fftSize - j].r = stft_matrix[i][j].real();
                in[fftSize - j].i = -stft_matrix[i][j].imag();
            }

            //inverse FFT
            kiss_fft(ifftCfg, in.data(), out.data());
            
            //extract and window
            int startIdx = i * hopSize;
            for (int j = 0; j < frameSize && startIdx + j < signalLength; j++) {
                float windowed = (out[j].r / fftSize) * window[j];
                reconstructed[startIdx + j] += windowed;
                windowSum[startIdx + j] += window[j] * window[j];
            }
            
            //report progress during reconstruction
            if (i % (numF / 10 + 1) == 0) {
                int progress = 90 + (int)(9.0f * i / numF);
                reportProgress(progress, "Reconstructing audio...");
            }
        }
        
        free(ifftCfg);

        //normalize by window sum
        for (int i = 0; i < signalLength; i++) {
            if (windowSum[i] > 1e-8f) {
                reconstructed[i] /= windowSum[i];
            }
        }
        
        return reconstructed;
    };
    
    std::vector<float> bg = istft(background_STFT);
    std::vector<float> fg_raw = istft(foreground_STFT);

    if (bg.empty() || fg_raw.empty()) {
        LOGE("Inverse STFT failed");
        return false;
    }
    
    //pad to match input length
    if (bg.size() < audioData.size()) {
        bg.resize(audioData.size(), 0.0f);
    } else if (bg.size() > audioData.size()) {
        bg.resize(audioData.size());
    }
    
    if (fg_raw.size() < audioData.size()) {
        fg_raw.resize(audioData.size(), 0.0f);
    } else if (fg_raw.size() > audioData.size()) {
        fg_raw.resize(audioData.size());
    }
    
    //compute foreground as difference
    std::vector<float> fg(audioData.size());
    for (size_t i = 0; i < audioData.size(); i++) {
        fg[i] = audioData[i] - bg[i];
    }
    
    //apply high-pass filter
    highpassFilter(fg, params.highpassCutoff, sampleRate);
    
    //background HPF adjustment: shift low-frequency components to background
    //this creates background_hpf = bg + (fg_original - fg_hpf)
    std::vector<float> bg_hpf = bg;
    for (size_t i = 0; i < bg_hpf.size(); i++) {
        bg_hpf[i] = bg[i] + (audioData[i] - bg[i] - fg[i]);
    }
    
    background = bg_hpf;
    foreground = fg;
    
    reportProgress(100, "Separation complete!");
    LOGD("REPET: Separation complete");
    return true;
}

/*
 * Apply REPET with windowed processing for long audio files
 */
bool repetSeparationWindowed(
    const std::vector<float>& audioData,
    uint32_t sampleRate,
    std::vector<float>& background,
    std::vector<float>& foreground,
    float windowSec,
    float overlap,
    const RepetParams& baseParams) {
    
    //validate parameters
    if (windowSec <= 0 || overlap <= 0 || overlap >= 1) {
        LOGE("Invalid windowed processing parameters");
        return false;
    }
    
    int windowSamples = (int)(windowSec * sampleRate);
    int hopSamples = (int)(windowSamples * (1.0f - overlap));
    
    background.assign(audioData.size(), 0.0f);
    foreground.assign(audioData.size(), 0.0f);
    std::vector<float> normalization(audioData.size(), 0.0f);
    
    //create Hann window for overlap-add
    std::vector<float> fadeWindow(windowSamples);
    for (int i = 0; i < windowSamples; i++) {
        fadeWindow[i] = 0.5f * (1.0f - std::cos(2.0f * 3.14159265f * i / (windowSamples - 1)));
    }
    
    int numWindows = (int)std::ceil((float)(audioData.size() - windowSamples) / hopSamples) + 1;
    LOGD("Windowed REPET: %d windows of %.1fs each, hop=%.1fs", numWindows, windowSec, 
         hopSamples / (float)sampleRate);
    
    //process each window
    for (int winIdx = 0; winIdx < numWindows; winIdx++) {
        int start = winIdx * hopSamples;
        int end = std::min(start + windowSamples, (int)audioData.size());
        
        //extract window
        std::vector<float> windowData(audioData.begin() + start, audioData.begin() + end);
        
        //pad if necessary (for last window)
        if ((int)windowData.size() < windowSamples) {
            windowData.resize(windowSamples, 0.0f);
        }
        
        //apply REPET to this window
        std::vector<float> bgWindow, fgWindow;
        RepetParams params = baseParams;
        params.windowSec = windowSec;
        // report progress at window granularity in windowed mode.
        params.progressCallback = nullptr;
        
        if (!repetSeparation(windowData, sampleRate, bgWindow, fgWindow, params)) {
            LOGE("REPET failed on window %d", winIdx);
            return false;
        }
        
        //trim back to actual length
        int actualLen = std::min(end - start, (int)bgWindow.size());
        bgWindow.resize(actualLen);
        fgWindow.resize(actualLen);
        
        //apply fade window for smooth overlap-add
        std::vector<float> fade(actualLen);
        for (int i = 0; i < actualLen; i++) {
            fade[i] = (i < windowSamples) ? fadeWindow[i] : 1.0f;
        }
        
        //overlap-add
        for (int i = 0; i < actualLen; i++) {
            int idx = start + i;
            background[idx] += bgWindow[i] * fade[i];
            foreground[idx] += fgWindow[i] * fade[i];
            normalization[idx] += fade[i];
        }
        
        //report progress
        int processed = winIdx + 1;
        int progress = (int)(100.0f * processed / std::max(numWindows, 1));
        std::string msg = "Processing window " + std::to_string(processed) + "/" + std::to_string(numWindows);
        if (baseParams.progressCallback) {
            baseParams.progressCallback(progress, msg.c_str());
        }
        
        LOGD("Windowed REPET: Window %d/%d complete", winIdx + 1, numWindows);
    }
    
    //normalize by overlap window
    for (size_t i = 0; i < audioData.size(); i++) {
        if (normalization[i] > 1e-8f) {
            background[i] /= normalization[i];
            foreground[i] /= normalization[i];
        }
    }
    
    if (baseParams.progressCallback) {
        baseParams.progressCallback(100, "Windowed separation complete!");
    }
    LOGD("Windowed REPET: Separation complete");
    return true;
}
