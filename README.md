# BeatPeeler

BeatPeeler is an Android audio-separation application for ECE 420. It takes a
mono or stereo WAV file, separates the repeating background from the remaining
foreground audio using a windowed REPET implementation, and lets you preview
or save the resulting tracks.

## Features

- Select a `.wav` file using the Android document picker.
- Configure the separation window from 2 to 30 seconds.
- Configure window overlap from 10% to 90%.
- Monitor separation progress while processing runs in the native layer.
- Play and scrub through the original, background, and foreground tracks.
- Export the original or separated tracks as WAV files to the device's
	`Downloads` folder.

## Requirements

- Android Studio with Android SDK 34 installed.
- Android SDK Build Tools and an Android SDK platform for API 34.
- Android NDK and CMake support enabled in Android Studio.
- A physical Android device or emulator running Android 9 (API 28) or newer.

The project uses Gradle 8.7 and Android Gradle Plugin 8.5.1. The app targets
Android 10 (API 29) and has a minimum supported version of Android 9 (API 28).

## Build and Run

1. Open the project directory in Android Studio.
2. Allow Android Studio to sync the Gradle project and install any requested
	 SDK, NDK, or CMake components.
3. Connect an Android device with USB debugging enabled, or start an emulator.
4. Select the `app` configuration and run it.

The debug APK can also be built from the project root:

```bash
./gradlew assembleDebug
```

The resulting APK is written to
`app/build/outputs/apk/debug/app-debug.apk`.

## Usage

1. Launch **BeatPeeler**.
2. Tap **Choose WAV File** and select a WAV audio file.
3. Adjust **Window** and **Overlap** if needed. The defaults are 10 seconds
	 and 25% overlap.
4. Tap **Split Song** and wait for processing to finish.
5. Use the play buttons and seek bars to compare the original, background, and
	 foreground tracks.
6. Use a track's download button to save it to the device's `Downloads`
	 folder.

The separated files are generated as `background.wav` and `foreground.wav`.
They are kept in the app's private cache until exported, so running the app or
processing another file does not create permanent output files automatically.

## Project Structure

```text
app/src/main/java/com/ece420/lab5/MainActivity.java  Android UI and playback
app/src/main/cpp/                                    Native audio processing
app/src/main/cpp/repet_separation.cpp                Windowed REPET algorithm
app/src/main/cpp/ece420_main.cpp                     JNI bridge and processing
app/src/main/res/layout/activity_main.xml            Main screen layout
```

The Java activity copies the selected document into the app cache, calls the
native `splitAudioFile` JNI method, receives progress callbacks, and manages
playback/export. The native library is built as `echo` through CMake and uses
Kiss FFT for frequency-domain processing.

## Permissions

The manifest declares `RECORD_AUDIO` and `MODIFY_AUDIO_SETTINGS` for the
project's native audio support. The current file-separation workflow reads the
selected WAV through the Android document picker and does not require recording
from the microphone.

## Troubleshooting

- **Build fails during native configuration:** verify that Android NDK and
	CMake are installed through Android Studio's SDK Manager.
- **A file is rejected:** choose a file with a `.wav` extension and a valid
	RIFF/WAVE header.
- **Separation fails:** use Android Studio Logcat and filter by `ECE420` for
	native processing details.
- **Output is not visible in the app:** processing results remain in private
	cache until the download button is pressed; check the device's `Downloads`
	folder after export.
