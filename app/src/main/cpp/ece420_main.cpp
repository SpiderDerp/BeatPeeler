#include <jni.h>
#include "ece420_main.h"
#include "ece420_lib.h"
#include "kiss_fft/kiss_fft.h"
#include "audio_io.h"
#include "repet_separation.h"
#include <string>
#include <functional>

// Global JNI callback for progress updates
static JNIEnv* g_jni_env = nullptr;
static jobject g_main_activity = nullptr;
static jmethodID g_update_progress_method = nullptr;

// Helper function to call Java progress update method
void reportProgress(int progress, const char* message) {
    if (g_jni_env && g_main_activity && g_update_progress_method) {
        jstring jmsg = g_jni_env->NewStringUTF(message ? message : "");
        g_jni_env->CallVoidMethod(g_main_activity, g_update_progress_method, progress, jmsg);
        g_jni_env->DeleteLocalRef(jmsg);
    }
}

// JNI Function
extern "C" {
JNIEXPORT void JNICALL
Java_com_ece420_lab5_MainActivity_writeNewFreq(JNIEnv *env, jclass, jint);

JNIEXPORT jboolean JNICALL
Java_com_ece420_lab5_MainActivity_splitAudioFile(JNIEnv *env, jobject mainActivityObj, jstring inputPath,
                                                 jstring outputDir, jfloat windowSec, jfloat overlap);
}

// Student Variables
#define EPOCH_PEAK_REGION_WIGGLE 30
#define VOICED_THRESHOLD 200000000
#define FRAME_SIZE 1024
#define BUFFER_SIZE (3 * FRAME_SIZE)
#define F_S 48000
float bufferIn[BUFFER_SIZE] = {};
float bufferOut[BUFFER_SIZE] = {};
int newEpochIdx = FRAME_SIZE;

// We have two variables here to ensure that we never change the desired frequency while
// processing a frame. Thread synchronization, etc. Setting to 300 is only an initializer.
int FREQ_NEW_ANDROID = 300;
int FREQ_NEW = 300;

bool lab5PitchShift(float *bufferIn) {
    // Lab 4 code is condensed into this function
    int periodLen = detectBufferPeriod(bufferIn);
    float freq = ((float) F_S) / periodLen;

    // If voiced
    if (periodLen > 0) {

        LOGD("Frequency detected: %f\r\n", freq);

        // Epoch detection - this code is written for you, but the principles will be quizzed
        std::vector<int> epochLocations;
        findEpochLocations(epochLocations, bufferIn, periodLen);

        // In this section, you will implement the algorithm given in:
        // https://courses.engr.illinois.edu/ece420/lab5/lab/#buffer-manipulation-algorithm
        //
        // Don't forget about the following functions! API given on the course page.
        //
        // getHanningCoef();
        // findClosestInVector();
        // overlapAndAdd();
        // *********************** START YOUR CODE HERE  **************************** //
        
        //calculate epoch spcaing
        int new_epoch_spacing = (int) ((float) F_S / FREQ_NEW);

        // //clear output fbuffer
        // for (int i = 0; i < BUFFER_SIZE; i++) {
        //     bufferOut[i] = 0.0f;
        // }

        //lop over epochs
        for (int i = newEpochIdx; i < BUFFER_SIZE; i+= new_epoch_spacing) {
            //find closest epoch
            int idx = findClosestInVector(epochLocations, i, 0, epochLocations.size() - 1);
            int orig_epoch = epochLocations[idx];   

            //skip the edge
            if (idx == 0 || idx == epochLocations.size() - 1) {
                continue;
            }

            //compute p0 pitch period
            int p0 = (epochLocations[idx + 1] - epochLocations[idx - 1]) / 2;

            //window boundaries
            int win_start = orig_epoch - p0;
            int win_end = orig_epoch + p0;
            int win_len = win_end - win_start;

            //check bounds
            if (win_start < 0 || win_end > BUFFER_SIZE) {
                continue;
            }

            //extract segment and plpy window
            float segment[BUFFER_SIZE];
            float window[BUFFER_SIZE];
            for (int j = 0; j < win_len; j++) {
                segment[j] = bufferIn[win_start + j];
                window[j] = getHanningCoef(win_len, j);
            }

            float windowedSegment[BUFFER_SIZE];
            for (int j = 0; j < win_len; j++) {
                windowedSegment[j] = segment[j] * window[j];
            }

            //place segment at new epoch
            int out_start = i - p0;
            if (out_start < 0 || out_start + win_len > BUFFER_SIZE) {
                continue;
            }
            overlapAddArray(bufferOut, windowedSegment, out_start, win_len);
        }

        // ************************ END YOUR CODE HERE  ***************************** //
    }

    // Final bookkeeping, move your new pointer back, because you'll be
    // shifting everything back now in your circular buffer
    newEpochIdx -= FRAME_SIZE;
    if (newEpochIdx < FRAME_SIZE) {
        newEpochIdx = FRAME_SIZE;
    }

    return (periodLen > 0);
}

void ece420ProcessFrame(sample_buf *dataBuf) {
    // Keep in mind, we only have 20ms to process each buffer!
    struct timeval start;
    struct timeval end;
    gettimeofday(&start, NULL);

    // Get the new desired frequency from android
    FREQ_NEW = FREQ_NEW_ANDROID;

    // Data is encoded in signed PCM-16, little-endian, mono
    int16_t data[FRAME_SIZE];
    for (int i = 0; i < FRAME_SIZE; i++) {
        data[i] = ((uint16_t) dataBuf->buf_[2 * i]) | (((uint16_t) dataBuf->buf_[2 * i + 1]) << 8);
    }

    // Shift our old data back to make room for the new data
    for (int i = 0; i < 2 * FRAME_SIZE; i++) {
        bufferIn[i] = bufferIn[i + FRAME_SIZE - 1];
    }

    // Finally, put in our new data.
    for (int i = 0; i < FRAME_SIZE; i++) {
        bufferIn[i + 2 * FRAME_SIZE - 1] = (float) data[i];
    }

    // The whole kit and kaboodle -- pitch shift
    bool isVoiced = lab5PitchShift(bufferIn);

    if (isVoiced) {
        for (int i = 0; i < FRAME_SIZE; i++) {
            int16_t newVal = (int16_t) bufferOut[i];

            uint8_t lowByte = (uint8_t) (0x00ff & newVal);
            uint8_t highByte = (uint8_t) ((0xff00 & newVal) >> 8);
            dataBuf->buf_[i * 2] = lowByte;
            dataBuf->buf_[i * 2 + 1] = highByte;
        }
    }

    // Very last thing, update your output circular buffer!
    for (int i = 0; i < 2 * FRAME_SIZE; i++) {
        bufferOut[i] = bufferOut[i + FRAME_SIZE - 1];
    }

    for (int i = 0; i < FRAME_SIZE; i++) {
        bufferOut[i + 2 * FRAME_SIZE - 1] = 0;
    }

    gettimeofday(&end, NULL);
    LOGD("Time delay: %ld us",  ((end.tv_sec * 1000000 + end.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec)));
}

// Returns lag l that maximizes sum(x[n] x[n-k])
int detectBufferPeriod(float *buffer) {

    float totalPower = 0;
    for (int i = 0; i < BUFFER_SIZE; i++) {
        totalPower += buffer[i] * buffer[i];
    }

    if (totalPower < VOICED_THRESHOLD) {
        return -1;
    }

    // FFT is done using Kiss FFT engine. Remember to free(cfg) on completion
    kiss_fft_cfg cfg = kiss_fft_alloc(BUFFER_SIZE, false, 0, 0);

    kiss_fft_cpx buffer_in[BUFFER_SIZE];
    kiss_fft_cpx buffer_fft[BUFFER_SIZE];

    for (int i = 0; i < BUFFER_SIZE; i++) {
        buffer_in[i].r = bufferIn[i];
        buffer_in[i].i = 0;
    }

    kiss_fft(cfg, buffer_in, buffer_fft);
    free(cfg);


    // Autocorrelation is given by:
    // autoc = ifft(fft(x) * conj(fft(x))
    //
    // Also, (a + jb) (a - jb) = a^2 + b^2
    kiss_fft_cfg cfg_ifft = kiss_fft_alloc(BUFFER_SIZE, true, 0, 0);

    kiss_fft_cpx multiplied_fft[BUFFER_SIZE];
    kiss_fft_cpx autoc_kiss[BUFFER_SIZE];

    for (int i = 0; i < BUFFER_SIZE; i++) {
        multiplied_fft[i].r = (buffer_fft[i].r * buffer_fft[i].r)
                              + (buffer_fft[i].i * buffer_fft[i].i);
        multiplied_fft[i].i = 0;
    }

    kiss_fft(cfg_ifft, multiplied_fft, autoc_kiss);
    free(cfg_ifft);

    // Move to a normal float array rather than a struct array of r/i components
    float autoc[BUFFER_SIZE];
    for (int i = 0; i < BUFFER_SIZE; i++) {
        autoc[i] = autoc_kiss[i].r;
    }

    // We're only interested in pitches below 1000Hz.
    // Why does this line guarantee we only identify pitches below 1000Hz?
    int minIdx = F_S / 1000;
    int maxIdx = BUFFER_SIZE / 2;

    int periodLen = findMaxArrayIdx(autoc, minIdx, maxIdx);
    float freq = ((float) F_S) / periodLen;

    // TODO: tune
    if (freq < 50) {
        periodLen = -1;
    }

    return periodLen;
}


void findEpochLocations(std::vector<int> &epochLocations, float *buffer, int periodLen) {
    // This algorithm requires that the epoch locations be pretty well marked

    int largestPeak = findMaxArrayIdx(bufferIn, 0, BUFFER_SIZE);
    epochLocations.push_back(largestPeak);

    // First go right
    int epochCandidateIdx = epochLocations[0] + periodLen;
    while (epochCandidateIdx < BUFFER_SIZE) {
        epochLocations.push_back(epochCandidateIdx);
        epochCandidateIdx += periodLen;
    }

    // Then go left
    epochCandidateIdx = epochLocations[0] - periodLen;
    while (epochCandidateIdx > 0) {
        epochLocations.push_back(epochCandidateIdx);
        epochCandidateIdx -= periodLen;
    }

    // Sort in place so that we can more easily find the period,
    // where period = (epochLocations[t+1] + epochLocations[t-1]) / 2
    std::sort(epochLocations.begin(), epochLocations.end());

    // Finally, just to make sure we have our epochs in the right
    // place, ensure that every epoch mark (sans first/last) sits on a peak
    for (int i = 1; i < epochLocations.size() - 1; i++) {
        int minIdx = epochLocations[i] - EPOCH_PEAK_REGION_WIGGLE;
        int maxIdx = epochLocations[i] + EPOCH_PEAK_REGION_WIGGLE;

        int peakOffset = findMaxArrayIdx(bufferIn, minIdx, maxIdx) - minIdx;
        peakOffset -= EPOCH_PEAK_REGION_WIGGLE;

        epochLocations[i] += peakOffset;
    }
}

void overlapAddArray(float *dest, float *src, int startIdx, int len) {
    int idxLow = startIdx;
    int idxHigh = startIdx + len;

    int padLow = 0;
    int padHigh = 0;
    if (idxLow < 0) {
        padLow = -idxLow;
    }
    if (idxHigh > BUFFER_SIZE) {
        padHigh = BUFFER_SIZE - idxHigh;
    }

    // Finally, reconstruct the buffer
    for (int i = padLow; i < len + padHigh; i++) {
        dest[startIdx + i] += src[i];
    }
}


JNIEXPORT void JNICALL
Java_com_ece420_lab5_MainActivity_writeNewFreq(JNIEnv *env, jclass, jint newFreq) {
    FREQ_NEW_ANDROID = (int) newFreq;
    return;
}

JNIEXPORT jboolean JNICALL
Java_com_ece420_lab5_MainActivity_splitAudioFile(JNIEnv *env, jobject mainActivityObj, jstring inputPath,
                                                 jstring outputDir, jfloat windowSec, jfloat overlap) {
    const char* inputPathStr = env->GetStringUTFChars(inputPath, nullptr);
    const char* outputDirStr = env->GetStringUTFChars(outputDir, nullptr);
    
    // Store JNI references for progress callbacks
    g_jni_env = env;
    if (g_main_activity) {
        env->DeleteGlobalRef(g_main_activity);
        g_main_activity = nullptr;
    }

    g_main_activity = env->NewGlobalRef(mainActivityObj);
    jclass mainActivityClass = env->GetObjectClass(mainActivityObj);
    if (!mainActivityClass) {
        LOGE("Failed to get MainActivity class");
        env->ReleaseStringUTFChars(inputPath, inputPathStr);
        env->ReleaseStringUTFChars(outputDir, outputDirStr);
        return false;
    }

    g_update_progress_method = env->GetMethodID(mainActivityClass, "updateProgress", "(ILjava/lang/String;)V");
    env->DeleteLocalRef(mainActivityClass);
    if (!g_update_progress_method) {
        LOGE("Failed to find updateProgress method");
        env->ReleaseStringUTFChars(inputPath, inputPathStr);
        env->ReleaseStringUTFChars(outputDir, outputDirStr);
        if (g_main_activity) {
            env->DeleteGlobalRef(g_main_activity);
            g_main_activity = nullptr;
        }
        return false;
    }
    
    LOGD("REPET Split: input=%s, outputDir=%s, windowSec=%.2f, overlap=%.2f", inputPathStr, outputDirStr,
         windowSec, overlap);
    reportProgress(0, "Loading audio file...");
    
    // Read input WAV file
    std::vector<float> audioData;
    uint32_t sampleRate = 0;
    
    if (!readWavFile(inputPathStr, audioData, sampleRate)) {
        LOGE("Failed to read input WAV file");
        env->ReleaseStringUTFChars(inputPath, inputPathStr);
        env->ReleaseStringUTFChars(outputDir, outputDirStr);
        return false;
    }
    
    LOGD("Loaded audio: %u samples @ %u Hz", (uint32_t)audioData.size(), sampleRate);
    reportProgress(5, "Audio loaded. Starting separation...");
    
    // Apply REPET algorithm
    std::vector<float> background, foreground;
    RepetParams params;
    params.frameSize = 2048;
    params.windowSec = (int)windowSec;
    params.minPeriodSec = 0.5f;
    params.maxPeriodSec = 4.0f;
    params.periodFrames = -1;  // Auto-detect
    params.highpassCutoff = 100.0f;
    params.progressCallback = reportProgress;

    if (!repetSeparationWindowed(audioData, sampleRate, background, foreground, windowSec, overlap, params)) {
        LOGE("Windowed REPET separation failed");
        env->ReleaseStringUTFChars(inputPath, inputPathStr);
        env->ReleaseStringUTFChars(outputDir, outputDirStr);
        if (g_main_activity) {
            env->DeleteGlobalRef(g_main_activity);
            g_main_activity = nullptr;
        }
        g_update_progress_method = nullptr;
        g_jni_env = nullptr;
        return false;
    }

    // if (!repetSeparation(audioData, sampleRate, background, foreground, params)) {
    //     LOGE("REPET separation failed");
    //     env->ReleaseStringUTFChars(inputPath, inputPathStr);
    //     env->ReleaseStringUTFChars(outputDir, outputDirStr);
    //     return false;
    // }
    
    reportProgress(85, "Writing output files...");
    
    // Write output WAV files
    std::string bgPath = std::string(outputDirStr) + "/background.wav";
    std::string fgPath = std::string(outputDirStr) + "/foreground.wav";
    
    if (!writeWavFile(bgPath, background, sampleRate)) {
        LOGE("Failed to write background WAV file");
        env->ReleaseStringUTFChars(inputPath, inputPathStr);
        env->ReleaseStringUTFChars(outputDir, outputDirStr);
        if (g_main_activity) {
            env->DeleteGlobalRef(g_main_activity);
            g_main_activity = nullptr;
        }
        g_update_progress_method = nullptr;
        g_jni_env = nullptr;
        return false;
    }
    
    if (!writeWavFile(fgPath, foreground, sampleRate)) {
        LOGE("Failed to write foreground WAV file");
        env->ReleaseStringUTFChars(inputPath, inputPathStr);
        env->ReleaseStringUTFChars(outputDir, outputDirStr);
        if (g_main_activity) {
            env->DeleteGlobalRef(g_main_activity);
            g_main_activity = nullptr;
        }
        g_update_progress_method = nullptr;
        g_jni_env = nullptr;
        return false;
    }
    
    reportProgress(100, "Separation complete!");
    LOGD("Successfully wrote background.wav and foreground.wav");
    
    env->ReleaseStringUTFChars(inputPath, inputPathStr);
    env->ReleaseStringUTFChars(outputDir, outputDirStr);
    if (g_main_activity) {
        env->DeleteGlobalRef(g_main_activity);
        g_main_activity = nullptr;
    }
    g_update_progress_method = nullptr;
    g_jni_env = nullptr;
    return true;
}