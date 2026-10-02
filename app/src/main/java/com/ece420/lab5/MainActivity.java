package com.ece420.lab5;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Intent;
import android.database.Cursor;
import android.media.MediaPlayer;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.MediaStore;
import android.provider.OpenableColumns;
import android.util.Log;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.view.WindowManager;
import android.webkit.MimeTypeMap;
import android.widget.Button;
import android.widget.ImageButton;
import android.widget.ProgressBar;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.core.app.ActivityCompat;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class MainActivity extends Activity
        implements ActivityCompat.OnRequestPermissionsResultCallback {

    private static final int REQUEST_CODE_PICK_WAV = 1001;
    private static final int MIN_WINDOW_SEC = 2;
    private static final int MAX_WINDOW_SEC = 30;

    private enum TrackType {
        NONE,
        ORIGINAL,
        BACKGROUND,
        FOREGROUND
    }

    TextView statusView;
    TextView selectedFileView;
    TextView windowSecValueView;
    TextView overlapValueView;
    ProgressBar progressBar;
    SeekBar windowSecSeekBar;
    SeekBar overlapSeekBar;
    Button splitSongButton;

    ImageButton originalPlayButton;
    ImageButton backgroundPlayButton;
    ImageButton foregroundPlayButton;
    ImageButton originalDownloadButton;
    ImageButton backgroundDownloadButton;
    ImageButton foregroundDownloadButton;
    SeekBar originalSeekBar;
    SeekBar backgroundSeekBar;
    SeekBar foregroundSeekBar;
    TextView originalTimeLabel;
    TextView backgroundTimeLabel;
    TextView foregroundTimeLabel;

    private Uri selectedAudioUri;
    private String selectedAudioName = "selected.wav";
    private float selectedWindowSec = 10.0f;
    private float selectedOverlap = 0.25f;
    private final Pattern windowProgressPattern = Pattern.compile("Processing window\\s+(\\d+)/(\\d+)");

    private MediaPlayer mediaPlayer;
    private String cacheDir;
    private TrackType activeTrack = TrackType.NONE;
    private boolean isUserScrubbing = false;

    private final Handler playbackUiHandler = new Handler(Looper.getMainLooper());
    private final Runnable playbackProgressUpdater = new Runnable() {
        @Override
        public void run() {
            if (mediaPlayer != null && activeTrack != TrackType.NONE) {
                SeekBar seekBar = getSeekBarForTrack(activeTrack);
                if (seekBar != null && !isUserScrubbing) {
                    seekBar.setProgress(mediaPlayer.getCurrentPosition());
                }
                updateTrackTimeLabel(activeTrack, mediaPlayer.getCurrentPosition(), mediaPlayer.getDuration());
                playbackUiHandler.postDelayed(this, 250);
            }
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        setContentView(R.layout.activity_main);

        statusView = (TextView) findViewById(R.id.statusView);
        selectedFileView = (TextView) findViewById(R.id.selected_file_view);
        windowSecValueView = (TextView) findViewById(R.id.window_sec_value);
        overlapValueView = (TextView) findViewById(R.id.overlap_value);
        progressBar = (ProgressBar) findViewById(R.id.progressBar);
        windowSecSeekBar = (SeekBar) findViewById(R.id.window_sec_seekbar);
        overlapSeekBar = (SeekBar) findViewById(R.id.overlap_seekbar);
        splitSongButton = (Button) findViewById(R.id.split_song_button);

        originalPlayButton = (ImageButton) findViewById(R.id.original_play_button);
        backgroundPlayButton = (ImageButton) findViewById(R.id.background_play_button);
        foregroundPlayButton = (ImageButton) findViewById(R.id.foreground_play_button);
        originalDownloadButton = (ImageButton) findViewById(R.id.original_download_button);
        backgroundDownloadButton = (ImageButton) findViewById(R.id.background_download_button);
        foregroundDownloadButton = (ImageButton) findViewById(R.id.foreground_download_button);
        originalSeekBar = (SeekBar) findViewById(R.id.original_seekbar);
        backgroundSeekBar = (SeekBar) findViewById(R.id.background_seekbar);
        foregroundSeekBar = (SeekBar) findViewById(R.id.foreground_seekbar);
        originalTimeLabel = (TextView) findViewById(R.id.original_time_label);
        backgroundTimeLabel = (TextView) findViewById(R.id.background_time_label);
        foregroundTimeLabel = (TextView) findViewById(R.id.foreground_time_label);

        cacheDir = getCacheDir().getAbsolutePath();

        setupParameterControls();
        setupPlaybackControls();
        statusView.setText("Ready to split audio.\nSelect a WAV file, then tap 'Split Song'.");
        selectedFileView.setText("Selected file: none");
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        releasePlayer();
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        getMenuInflater().inflate(R.menu.menu_main, menu);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        int id = item.getItemId();
        if (id == R.id.action_settings) {
            return true;
        }
        return super.onOptionsItemSelected(item);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions,
                                           @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
    }

    static {
        System.loadLibrary("echo");
    }

    public native boolean splitAudioFile(String inputPath, String outputDir, float windowSec, float overlap);

    public void updateProgress(final int progress, final String message) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (progressBar != null) {
                    Matcher matcher = windowProgressPattern.matcher(message == null ? "" : message);
                    if (matcher.find()) {
                        int processed = Integer.parseInt(matcher.group(1));
                        int total = Integer.parseInt(matcher.group(2));
                        progressBar.setMax(Math.max(total, 1));
                        progressBar.setProgress(Math.min(processed, total));
                    } else {
                        progressBar.setMax(100);
                        progressBar.setProgress(Math.max(0, Math.min(progress, 100)));
                    }
                }
                if (statusView != null && message != null) {
                    statusView.setText(message);
                }
            }
        });
    }

    private void setupParameterControls() {
        windowSecSeekBar.setMax(MAX_WINDOW_SEC - MIN_WINDOW_SEC);
        windowSecSeekBar.setProgress((int) selectedWindowSec - MIN_WINDOW_SEC);
        windowSecValueView.setText(formatWindowLabel(selectedWindowSec));

        overlapSeekBar.setMax(80);
        overlapSeekBar.setProgress((int) (selectedOverlap * 100.0f) - 10);
        overlapValueView.setText(formatOverlapLabel(selectedOverlap));

        windowSecSeekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                selectedWindowSec = MIN_WINDOW_SEC + progress;
                windowSecValueView.setText(formatWindowLabel(selectedWindowSec));
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
            }
        });

        overlapSeekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                int overlapPercent = 10 + progress;
                selectedOverlap = overlapPercent / 100.0f;
                overlapValueView.setText(formatOverlapLabel(selectedOverlap));
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
            }
        });
    }

    private void setupPlaybackControls() {
        setTrackControlEnabled(TrackType.ORIGINAL, false);
        setTrackControlEnabled(TrackType.BACKGROUND, false);
        setTrackControlEnabled(TrackType.FOREGROUND, false);

        bindScrubbing(originalSeekBar, TrackType.ORIGINAL);
        bindScrubbing(backgroundSeekBar, TrackType.BACKGROUND);
        bindScrubbing(foregroundSeekBar, TrackType.FOREGROUND);
        updateTrackTimeLabel(TrackType.ORIGINAL, 0, 0);
        updateTrackTimeLabel(TrackType.BACKGROUND, 0, 0);
        updateTrackTimeLabel(TrackType.FOREGROUND, 0, 0);

        updatePlayIcons();
    }

    private void bindScrubbing(final SeekBar seekBar, final TrackType track) {
        seekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar bar, int progress, boolean fromUser) {
                if (fromUser && mediaPlayer != null && activeTrack == track) {
                    mediaPlayer.seekTo(progress);
                    updateTrackTimeLabel(track, progress, mediaPlayer.getDuration());
                }
            }

            @Override
            public void onStartTrackingTouch(SeekBar bar) {
                if (activeTrack == track) {
                    isUserScrubbing = true;
                }
            }

            @Override
            public void onStopTrackingTouch(SeekBar bar) {
                if (activeTrack == track) {
                    isUserScrubbing = false;
                    if (mediaPlayer != null) {
                        mediaPlayer.seekTo(bar.getProgress());
                        updateTrackTimeLabel(track, bar.getProgress(), mediaPlayer.getDuration());
                    }
                }
            }
        });
    }

    private String formatWindowLabel(float windowSec) {
        return "Window: " + (int) windowSec + " sec";
    }

    private String formatOverlapLabel(float overlap) {
        return "Overlap: " + (int) (overlap * 100.0f) + "%";
    }

    public void chooseWavFile(View view) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("audio/*");
        intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[]{"audio/wav", "audio/x-wav"});
        startActivityForResult(intent, REQUEST_CODE_PICK_WAV);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);

        if (requestCode != REQUEST_CODE_PICK_WAV || resultCode != RESULT_OK || data == null) {
            return;
        }

        Uri uri = data.getData();
        if (uri == null) {
            statusView.setText("No file selected.");
            return;
        }

        String name = getDisplayName(uri);
        if (!name.toLowerCase().endsWith(".wav") && !isWavMimeType(uri)) {
            statusView.setText("Please choose a .wav file.");
            return;
        }

        selectedAudioUri = uri;
        selectedAudioName = name;
        selectedFileView.setText("Selected file: " + name);
        setTrackControlEnabled(TrackType.ORIGINAL, true);
        resetTrackUi(TrackType.ORIGINAL);
        statusView.setText("WAV loaded. Tap 'Split Song' to begin.");
    }

    private String getDisplayName(Uri uri) {
        Cursor cursor = null;
        try {
            cursor = getContentResolver().query(uri, null, null, null, null);
            if (cursor != null && cursor.moveToFirst()) {
                int idx = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (idx >= 0) {
                    String name = cursor.getString(idx);
                    if (name != null && !name.isEmpty()) {
                        return name;
                    }
                }
            }
        } catch (Exception e) {
            Log.w("ECE420", "Could not read display name: " + e.getMessage());
        } finally {
            if (cursor != null) {
                cursor.close();
            }
        }

        String path = uri.getLastPathSegment();
        if (path == null || path.isEmpty()) {
            return "selected.wav";
        }
        int slash = path.lastIndexOf('/');
        if (slash >= 0 && slash + 1 < path.length()) {
            return path.substring(slash + 1);
        }
        return path;
    }

    private boolean isWavMimeType(Uri uri) {
        String type = getContentResolver().getType(uri);
        if (type == null) {
            String ext = MimeTypeMap.getFileExtensionFromUrl(uri.toString());
            return "wav".equalsIgnoreCase(ext);
        }
        return "audio/wav".equalsIgnoreCase(type) || "audio/x-wav".equalsIgnoreCase(type);
    }

    private boolean copyUriToFile(Uri uri, String outPath) {
        InputStream is = null;
        FileOutputStream fos = null;
        try {
            is = getContentResolver().openInputStream(uri);
            if (is == null) {
                return false;
            }
            fos = new FileOutputStream(outPath);
            byte[] buffer = new byte[4096];
            int len;
            while ((len = is.read(buffer)) > 0) {
                fos.write(buffer, 0, len);
            }
            fos.flush();
            return true;
        } catch (IOException e) {
            Log.e("ECE420", "Failed to copy selected file: " + e.getMessage());
            return false;
        } finally {
            closeQuietly(is);
            closeQuietly(fos);
        }
    }

    public void splitSong(View view) {
        if (selectedAudioUri == null) {
            statusView.setText("Please choose a WAV file first.");
            return;
        }

        try {
            final String inputPath = cacheDir + "/input.wav";
            final String outputDir = cacheDir;
            final float windowSec = selectedWindowSec;
            final float overlap = selectedOverlap;

            Log.d("ECE420", "Starting split process...");
            if (!copyUriToFile(selectedAudioUri, inputPath)) {
                statusView.setText("ERROR: Could not load selected WAV file\n\nCheck logcat for details");
                return;
            }

            progressBar.setVisibility(View.VISIBLE);
            progressBar.setMax(100);
            progressBar.setProgress(0);
            splitSongButton.setEnabled(false);
            statusView.setText("Processing...\nPlease wait");

            Thread worker = new Thread(new Runnable() {
                @Override
                public void run() {
                    final boolean success = splitAudioFile(inputPath, outputDir, windowSec, overlap);
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            progressBar.setVisibility(View.GONE);
                            splitSongButton.setEnabled(true);

                            if (success) {
                                setTrackControlEnabled(TrackType.BACKGROUND, true);
                                setTrackControlEnabled(TrackType.FOREGROUND, true);
                                resetTrackUi(TrackType.BACKGROUND);
                                resetTrackUi(TrackType.FOREGROUND);
                                statusView.setText("Success!\nOutputs are ready to play or download.");
                            } else {
                                setTrackControlEnabled(TrackType.BACKGROUND, false);
                                setTrackControlEnabled(TrackType.FOREGROUND, false);
                                statusView.setText("ERROR: Failed during splitting process\n\nCheck logcat (tag: ECE420) for details");
                            }
                        }
                    });
                }
            });
            worker.start();

        } catch (Exception e) {
            statusView.setText("ERROR: " + e.getMessage());
            progressBar.setVisibility(View.GONE);
            splitSongButton.setEnabled(true);
        }
    }

    public void toggleOriginalPlayback(View view) {
        toggleTrackPlayback(TrackType.ORIGINAL);
    }

    public void toggleBackgroundPlayback(View view) {
        toggleTrackPlayback(TrackType.BACKGROUND);
    }

    public void toggleForegroundPlayback(View view) {
        toggleTrackPlayback(TrackType.FOREGROUND);
    }

    private void toggleTrackPlayback(TrackType track) {
        if (!trackHasSource(track)) {
            statusView.setText("Track is not available yet.");
            return;
        }

        try {
            if (activeTrack != track || mediaPlayer == null) {
                prepareTrack(track);
                startPlaybackLoop();
                return;
            }

            if (mediaPlayer.isPlaying()) {
                mediaPlayer.pause();
                statusView.setText(trackLabel(track) + " paused");
            } else {
                mediaPlayer.start();
                statusView.setText("Playing " + trackLabel(track));
                startPlaybackLoop();
            }
            updatePlayIcons();
        } catch (Exception e) {
            Log.e("ECE420", "Playback error: " + e.getMessage());
            statusView.setText("Playback error: " + e.getMessage());
        }
    }

    private void prepareTrack(final TrackType track) throws IOException {
        releasePlayer();
        mediaPlayer = new MediaPlayer();

        if (track == TrackType.ORIGINAL) {
            mediaPlayer.setDataSource(this, selectedAudioUri);
        } else {
            File file = new File(trackFilePath(track));
            if (!file.exists()) {
                throw new IOException(trackLabel(track) + " file not found");
            }
            if (!isValidWavFile(file)) {
                throw new IOException(trackLabel(track) + " file is not a valid WAV");
            }
            mediaPlayer.setDataSource(file.getAbsolutePath());
        }

        mediaPlayer.setOnCompletionListener(new MediaPlayer.OnCompletionListener() {
            @Override
            public void onCompletion(MediaPlayer mp) {
                SeekBar seekBar = getSeekBarForTrack(track);
                if (seekBar != null) {
                    seekBar.setProgress(seekBar.getMax());
                }
                updateTrackTimeLabel(track, mediaPlayer != null ? mediaPlayer.getDuration() : 0,
                        mediaPlayer != null ? mediaPlayer.getDuration() : 0);
                statusView.setText(trackLabel(track) + " complete");
                updatePlayIcons();
                stopPlaybackLoop();
            }
        });

        mediaPlayer.setOnErrorListener(new MediaPlayer.OnErrorListener() {
            @Override
            public boolean onError(MediaPlayer mp, int what, int extra) {
                statusView.setText("Playback error code " + what + ", extra " + extra);
                releasePlayer();
                return true;
            }
        });

        mediaPlayer.prepare();
        activeTrack = track;

        SeekBar seekBar = getSeekBarForTrack(track);
        if (seekBar != null) {
            seekBar.setMax(mediaPlayer.getDuration());
            seekBar.setProgress(0);
        }
        updateTrackTimeLabel(track, 0, mediaPlayer.getDuration());

        mediaPlayer.start();
        statusView.setText("Playing " + trackLabel(track));
        updatePlayIcons();
        resetOtherTrackSeekbars(track);
    }

    private void resetOtherTrackSeekbars(TrackType keepTrack) {
        if (keepTrack != TrackType.ORIGINAL) {
            originalSeekBar.setProgress(0);
        }
        if (keepTrack != TrackType.BACKGROUND) {
            backgroundSeekBar.setProgress(0);
        }
        if (keepTrack != TrackType.FOREGROUND) {
            foregroundSeekBar.setProgress(0);
        }
    }

    private void startPlaybackLoop() {
        playbackUiHandler.removeCallbacks(playbackProgressUpdater);
        playbackUiHandler.post(playbackProgressUpdater);
    }

    private void stopPlaybackLoop() {
        playbackUiHandler.removeCallbacks(playbackProgressUpdater);
    }

    private void releasePlayer() {
        stopPlaybackLoop();
        if (mediaPlayer != null) {
            try {
                mediaPlayer.stop();
            } catch (Exception ignored) {
            }
            try {
                mediaPlayer.release();
            } catch (Exception ignored) {
            }
        }
        mediaPlayer = null;
        activeTrack = TrackType.NONE;
        updatePlayIcons();
    }

    private void updatePlayIcons() {
        setPlayIcon(originalPlayButton, activeTrack == TrackType.ORIGINAL && mediaPlayer != null && mediaPlayer.isPlaying());
        setPlayIcon(backgroundPlayButton, activeTrack == TrackType.BACKGROUND && mediaPlayer != null && mediaPlayer.isPlaying());
        setPlayIcon(foregroundPlayButton, activeTrack == TrackType.FOREGROUND && mediaPlayer != null && mediaPlayer.isPlaying());
    }

    private void setPlayIcon(ImageButton button, boolean playing) {
        if (button == null) {
            return;
        }
        if (playing) {
            button.setImageResource(android.R.drawable.ic_media_pause);
        } else {
            button.setImageResource(android.R.drawable.ic_media_play);
        }
    }

    private SeekBar getSeekBarForTrack(TrackType track) {
        if (track == TrackType.ORIGINAL) {
            return originalSeekBar;
        }
        if (track == TrackType.BACKGROUND) {
            return backgroundSeekBar;
        }
        if (track == TrackType.FOREGROUND) {
            return foregroundSeekBar;
        }
        return null;
    }

    private TextView getTimeLabelForTrack(TrackType track) {
        if (track == TrackType.ORIGINAL) {
            return originalTimeLabel;
        }
        if (track == TrackType.BACKGROUND) {
            return backgroundTimeLabel;
        }
        if (track == TrackType.FOREGROUND) {
            return foregroundTimeLabel;
        }
        return null;
    }

    private void updateTrackTimeLabel(TrackType track, int currentMs, int totalMs) {
        TextView label = getTimeLabelForTrack(track);
        if (label == null) {
            return;
        }
        int safeCurrent = Math.max(0, currentMs);
        int safeTotal = Math.max(0, totalMs);
        label.setText(formatDuration(safeCurrent) + " / " + formatDuration(safeTotal));
    }

    private String formatDuration(int millis) {
        int totalSeconds = millis / 1000;
        int hours = totalSeconds / 3600;
        int minutes = (totalSeconds % 3600) / 60;
        int seconds = totalSeconds % 60;
        if (hours > 0) {
            return String.format(Locale.US, "%d:%02d:%02d", hours, minutes, seconds);
        }
        return String.format(Locale.US, "%02d:%02d", minutes, seconds);
    }

    private void setTrackControlEnabled(TrackType track, boolean enabled) {
        if (track == TrackType.ORIGINAL) {
            originalPlayButton.setEnabled(enabled);
            originalSeekBar.setEnabled(enabled);
            originalDownloadButton.setEnabled(enabled);
        } else if (track == TrackType.BACKGROUND) {
            backgroundPlayButton.setEnabled(enabled);
            backgroundSeekBar.setEnabled(enabled);
            backgroundDownloadButton.setEnabled(enabled);
        } else if (track == TrackType.FOREGROUND) {
            foregroundPlayButton.setEnabled(enabled);
            foregroundSeekBar.setEnabled(enabled);
            foregroundDownloadButton.setEnabled(enabled);
        }
    }

    private void resetTrackUi(TrackType track) {
        SeekBar seekBar = getSeekBarForTrack(track);
        if (seekBar != null) {
            seekBar.setProgress(0);
            seekBar.setMax(100);
        }
        updateTrackTimeLabel(track, 0, 0);
        updatePlayIcons();
    }

    private boolean trackHasSource(TrackType track) {
        if (track == TrackType.ORIGINAL) {
            return selectedAudioUri != null;
        }
        File file = new File(trackFilePath(track));
        return file.exists() && file.length() > 44;
    }

    private String trackFilePath(TrackType track) {
        if (track == TrackType.BACKGROUND) {
            return cacheDir + "/background.wav";
        }
        if (track == TrackType.FOREGROUND) {
            return cacheDir + "/foreground.wav";
        }
        return "";
    }

    private String trackLabel(TrackType track) {
        if (track == TrackType.ORIGINAL) {
            return "original";
        }
        if (track == TrackType.BACKGROUND) {
            return "background";
        }
        if (track == TrackType.FOREGROUND) {
            return "foreground";
        }
        return "track";
    }

    public void downloadOriginalTrack(View view) {
        if (selectedAudioUri == null) {
            statusView.setText("No original file to download.");
            return;
        }
        String name = ensureWavName("original_" + stripExtension(selectedAudioName));
        downloadFromUri(selectedAudioUri, name);
    }

    public void downloadBackgroundTrack(View view) {
        downloadFromFile(trackFilePath(TrackType.BACKGROUND), "background_output.wav");
    }

    public void downloadForegroundTrack(View view) {
        downloadFromFile(trackFilePath(TrackType.FOREGROUND), "foreground_output.wav");
    }

    private void downloadFromFile(String inputPath, String outputName) {
        File source = new File(inputPath);
        if (!source.exists()) {
            statusView.setText("Track is not available for download.");
            return;
        }

        InputStream in = null;
        try {
            in = new FileInputStream(source);
            Uri uri = writeToDownloads(in, outputName);
            if (uri != null) {
                statusView.setText("Saved to Downloads: " + outputName);
                Toast.makeText(this, "Saved " + outputName, Toast.LENGTH_SHORT).show();
            } else {
                statusView.setText("Download failed.");
            }
        } catch (Exception e) {
            statusView.setText("Download failed: " + e.getMessage());
        } finally {
            closeQuietly(in);
        }
    }

    private void downloadFromUri(Uri sourceUri, String outputName) {
        InputStream in = null;
        try {
            in = getContentResolver().openInputStream(sourceUri);
            if (in == null) {
                statusView.setText("Could not read selected track.");
                return;
            }
            Uri uri = writeToDownloads(in, outputName);
            if (uri != null) {
                statusView.setText("Saved to Downloads: " + outputName);
                Toast.makeText(this, "Saved " + outputName, Toast.LENGTH_SHORT).show();
            } else {
                statusView.setText("Download failed.");
            }
        } catch (Exception e) {
            statusView.setText("Download failed: " + e.getMessage());
        } finally {
            closeQuietly(in);
        }
    }

    private Uri writeToDownloads(InputStream in, String fileName) throws IOException {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            ContentValues values = new ContentValues();
            values.put(MediaStore.MediaColumns.DISPLAY_NAME, fileName);
            values.put(MediaStore.MediaColumns.MIME_TYPE, "audio/wav");
            values.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS);

            ContentResolver resolver = getContentResolver();
            Uri uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
            if (uri == null) {
                return null;
            }

            OutputStream out = null;
            try {
                out = resolver.openOutputStream(uri);
                if (out == null) {
                    return null;
                }
                copyStream(in, out);
                out.flush();
                return uri;
            } finally {
                closeQuietly(out);
            }
        }

        File downloadsDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
        if (!downloadsDir.exists() && !downloadsDir.mkdirs()) {
            return null;
        }

        File outFile = new File(downloadsDir, fileName);
        OutputStream out = null;
        try {
            out = new FileOutputStream(outFile);
            copyStream(in, out);
            out.flush();
            return Uri.fromFile(outFile);
        } finally {
            closeQuietly(out);
        }
    }

    private void copyStream(InputStream in, OutputStream out) throws IOException {
        byte[] buffer = new byte[4096];
        int len;
        while ((len = in.read(buffer)) > 0) {
            out.write(buffer, 0, len);
        }
    }

    private String stripExtension(String value) {
        int dot = value.lastIndexOf('.');
        if (dot > 0) {
            return value.substring(0, dot);
        }
        return value;
    }

    private String ensureWavName(String name) {
        if (name.toLowerCase().endsWith(".wav")) {
            return name;
        }
        return name + ".wav";
    }

    private void closeQuietly(InputStream stream) {
        if (stream != null) {
            try {
                stream.close();
            } catch (IOException ignored) {
            }
        }
    }

    private void closeQuietly(OutputStream stream) {
        if (stream != null) {
            try {
                stream.close();
            } catch (IOException ignored) {
            }
        }
    }

    private boolean isValidWavFile(File file) {
        java.io.RandomAccessFile raf = null;
        try {
            raf = new java.io.RandomAccessFile(file, "r");

            byte[] riff = new byte[4];
            raf.read(riff);
            if (!new String(riff).equals("RIFF")) {
                return false;
            }

            raf.readInt();

            byte[] wave = new byte[4];
            raf.read(wave);
            return new String(wave).equals("WAVE");
        } catch (Exception e) {
            return false;
        } finally {
            if (raf != null) {
                try {
                    raf.close();
                } catch (IOException ignored) {
                }
            }
        }
    }
}
