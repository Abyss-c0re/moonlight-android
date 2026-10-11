package com.limelight.vr;

import android.app.Activity;
import android.content.Context;
import android.graphics.SurfaceTexture;
import android.net.ConnectivityManager;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.view.Surface;

import com.limelight.LimeLog;
import com.limelight.binding.PlatformBinding;
import com.limelight.binding.audio.AndroidAudioRenderer;
import com.limelight.binding.video.CrashListener;
import com.limelight.binding.video.MediaCodecDecoderRenderer;
import com.limelight.binding.video.MediaCodecHelper;
import com.limelight.binding.video.PerfOverlayListener;
import com.limelight.nvstream.NvConnection;
import com.limelight.nvstream.NvConnectionListener;
import com.limelight.nvstream.StreamConfiguration;
import com.limelight.nvstream.http.ComputerDetails;
import com.limelight.nvstream.http.NvApp;
import com.limelight.nvstream.input.MouseButtonPacket;
import com.limelight.nvstream.jni.MoonBridge;
import com.limelight.preferences.GlPreferences;
import com.limelight.preferences.PreferenceConfiguration;

/**
 * One Moonlight stream drawn on every non-menu VR screen.
 * The SurfaceTexture is created and updated on the OpenXR GL thread.
 * HDR and gamepad mapping stay on the flat player.
 */
final class VrStreamHost {
    static final int PTR_DOWN = 0;
    static final int PTR_MOVE = 1;
    static final int PTR_UP = 2;
    static final int PTR_SCROLL = 3;

    private final Object gate = new Object();
    private final Handler main = new Handler(Looper.getMainLooper());

    private Activity activity;
    private volatile SurfaceTexture surfaceTexture;
    private Surface surface;
    private VrDecoderSurface holder;
    private volatile boolean frameAvailable;
    private volatile boolean waitForNewFrame;
    private long presentedTimestamp = -1;

    private NvConnection conn;
    private int publishedToken;
    private int tokenSource;
    private int inputToken;
    private int refWidth;
    private int refHeight;
    private int currentAppId = -1;
    private String currentUuid;
    private WifiManager.WifiLock highPerfWifiLock;
    private WifiManager.WifiLock lowLatencyWifiLock;

    void attach(Activity activity) {
        this.activity = activity;
    }

    boolean isActive() {
        synchronized (gate) {
            return conn != null;
        }
    }

    /**
     * Called on the OpenXR thread. texId is a GL_TEXTURE_EXTERNAL_OES name.
     */
    void attachTexture(int texId) {
        releaseSurface();
        if (texId == 0) {
            return;
        }
        frameAvailable = false;
        final SurfaceTexture texture = new SurfaceTexture(texId);
        texture.setOnFrameAvailableListener(new SurfaceTexture.OnFrameAvailableListener() {
            @Override
            public void onFrameAvailable(SurfaceTexture ignored) {
                frameAvailable = true;
            }
        }, main);
        Surface decoded = new Surface(texture);
        surfaceTexture = texture;
        surface = decoded;
        holder = new VrDecoderSurface(decoded);
    }

    /** Called on the OpenXR thread after the decoder has stopped. */
    void releaseSurface() {
        SurfaceTexture texture = surfaceTexture;
        Surface decoded = surface;
        surfaceTexture = null;
        surface = null;
        holder = null;
        frameAvailable = false;
        if (texture != null) {
            texture.setOnFrameAvailableListener(null);
        }
        if (decoded != null) {
            decoded.release();
        }
        if (texture != null) {
            texture.release();
        }
    }

    /**
     * Called on the OpenXR thread. Returns 1 when a new frame is ready,
     * 0 when the picture has not changed, and -1 when the texture is gone.
     */
    int pullFrame(float[] matrix) {
        SurfaceTexture texture = surfaceTexture;
        if (texture == null || matrix == null || matrix.length < 16) {
            return -1;
        }
        if (!frameAvailable) {
            return 0;
        }
        frameAvailable = false;
        try {
            texture.updateTexImage();
        } catch (RuntimeException e) {
            return surfaceTexture == null ? -1 : 0;
        }
        long timestamp = texture.getTimestamp();
        if (timestamp <= 0) {
            return 0;
        }
        if (waitForNewFrame && timestamp == presentedTimestamp) {
            return 0;
        }
        waitForNewFrame = false;
        presentedTimestamp = timestamp;
        texture.getTransformMatrix(matrix);
        return 1;
    }

    void open(Activity activity, ComputerDetails computer, NvApp app, String uniqueId) {
        this.activity = activity;
        String name = app != null && app.getAppName() != null && app.getAppName().length() > 0
                ? app.getAppName() : "App";
        if (activity == null || activity.isFinishing() || computer == null || app == null || !app.isInitialized()) {
            status("Stream failed");
            return;
        }
        if (computer.activeAddress == null || computer.activeAddress.address == null || uniqueId == null) {
            status("Stream failed");
            return;
        }
        if (sameStream(computer.uuid, app.getAppId())) {
            status(name);
            return;
        }
        stop();
        status("Connecting");
        if (!start(activity, computer, app, uniqueId, name)) {
            stop();
            status("Stream failed");
        }
    }

    void stop() {
        NvConnection dying = detach();
        if (dying != null) {
            dying.stop();
        }
    }

    void onPointer(int phase, float u, float v, int button) {
        NvConnection current;
        int width;
        int height;
        synchronized (gate) {
            if (conn == null || inputToken == 0 || inputToken != publishedToken) {
                return;
            }
            current = conn;
            width = refWidth;
            height = refHeight;
        }
        if (width < 1 || height < 1) {
            return;
        }
        if (phase == PTR_SCROLL) {
            sendPosition(current, u, v, width, height);
            current.sendMouseScroll((byte) (button >= 0 ? 1 : -1));
            return;
        }
        byte mouseButton = button == 1 ? MouseButtonPacket.BUTTON_RIGHT : MouseButtonPacket.BUTTON_LEFT;
        sendPosition(current, u, v, width, height);
        if (phase == PTR_DOWN) {
            current.sendMouseButtonDown(mouseButton);
        } else if (phase == PTR_UP) {
            current.sendMouseButtonUp(mouseButton);
        }
    }

    private boolean sameStream(String uuid, int appId) {
        synchronized (gate) {
            return conn != null && appId == currentAppId && uuid != null && uuid.equals(currentUuid);
        }
    }

    private NvConnection detach() {
        synchronized (gate) {
            return detachLocked();
        }
    }

    private NvConnection detachLocked() {
        publishedToken = ++tokenSource;
        inputToken = 0;
        NvConnection dying = conn;
        conn = null;
        currentAppId = -1;
        currentUuid = null;
        refWidth = 0;
        refHeight = 0;
        waitForNewFrame = true;
        MoonlightXr.nativeClearVideo();
        releaseWifi();
        return dying;
    }

    private boolean isCurrent(int token) {
        synchronized (gate) {
            return token == publishedToken;
        }
    }

    private boolean start(Activity activity, ComputerDetails computer, NvApp app, String uniqueId, String name) {
        try {
            return startConfigured(activity, computer, app, uniqueId, name);
        } catch (RuntimeException e) {
            LimeLog.warning("VR stream failed to start: " + e.getMessage());
            return false;
        }
    }

    private boolean startConfigured(Activity activity, ComputerDetails computer, NvApp app, String uniqueId, String name) {
        VrDecoderSurface target = holder;
        SurfaceTexture texture = surfaceTexture;
        if (target == null || texture == null || surface == null || !surface.isValid()) {
            LimeLog.warning("VR stream has no video surface");
            return false;
        }
        PreferenceConfiguration prefs = PreferenceConfiguration.readPreferences(activity);
        int width = prefs.width > 0 ? prefs.width : 1280;
        int height = prefs.height > 0 ? prefs.height : 720;
        int fps = prefs.fps > 0 ? prefs.fps : 60;
        int bitrate = prefs.bitrate;
        /* The flat app still defaults to 720p. A monitor that fills the Neo 3
         * view needs at least 1080p or the desktop looks soft. */
        if (width < 1920 || height < 1080) {
            width = 1920;
            height = 1080;
            int floor = PreferenceConfiguration.getDefaultBitrate("1920x1080", Integer.toString(fps));
            if (bitrate < floor) {
                bitrate = floor;
            }
        }
        width = Math.min(width, Short.MAX_VALUE);
        height = Math.min(height, Short.MAX_VALUE);
        LimeLog.info("VR stream " + width + "x" + height + " @" + fps + " " + bitrate + "kbps");
        texture.setDefaultBufferSize(width, height);
        target.setBufferSize(width, height);

        GlPreferences glPrefs = GlPreferences.readPreferences(activity);
        MediaCodecHelper.initialize(activity, glPrefs.glRenderer);
        boolean metered = false;
        ConnectivityManager connectivity = (ConnectivityManager) activity.getSystemService(Context.CONNECTIVITY_SERVICE);
        if (connectivity != null) {
            metered = connectivity.isActiveNetworkMetered();
        }
        // HDR stays off here. The flat player owns that path, and this activity
        // does not change the headset display mode.
        MediaCodecDecoderRenderer decoder = new MediaCodecDecoderRenderer(
                activity,
                prefs,
                new CrashListener() {
                    @Override
                    public void notifyCrash(Exception e) {
                        LimeLog.warning("VR decoder crashed: " + e.getMessage());
                    }
                },
                0,
                metered,
                false,
                glPrefs.glRenderer,
                new PerfOverlayListener() {
                    @Override
                    public void onPerfUpdate(String text) {
                    }
                });
        if (!decoder.isAvcSupported()) {
            LimeLog.warning("VR stream found no H.264 decoder");
            return false;
        }
        int formats = MoonBridge.VIDEO_FORMAT_H264;
        if (decoder.isHevcSupported()) {
            formats |= MoonBridge.VIDEO_FORMAT_H265;
        }
        if (decoder.isAv1Supported()) {
            formats |= MoonBridge.VIDEO_FORMAT_AV1_MAIN8;
        }
        NvApp launched = new NvApp(name, app.getAppId(), false);
        StreamConfiguration config = new StreamConfiguration.Builder()
                .setResolution(width, height)
                .setLaunchRefreshRate(fps)
                .setRefreshRate(fps)
                .setApp(launched)
                .setBitrate(bitrate)
                .setEnableSops(prefs.enableSops)
                .enableLocalAudioPlayback(prefs.playHostAudio)
                .setMaxPacketSize(1392)
                .setRemoteConfiguration(StreamConfiguration.STREAM_CFG_AUTO)
                .setSupportedVideoFormats(formats)
                .setAttachedGamepadMask(0)
                .setClientRefreshRateX100(fps * 100)
                .setAudioConfiguration(prefs.audioConfiguration != null
                        ? prefs.audioConfiguration : MoonBridge.AUDIO_CONFIGURATION_STEREO)
                .setColorSpace(decoder.getPreferredColorSpace())
                .setColorRange(decoder.getPreferredColorRange())
                .setPersistGamepadsAfterDisconnect(false)
                .build();
        ComputerDetails.AddressTuple address = new ComputerDetails.AddressTuple(
                computer.activeAddress.address, computer.activeAddress.port);
        NvConnection created = new NvConnection(
                activity.getApplicationContext(),
                address,
                computer.httpsPort,
                uniqueId,
                config,
                PlatformBinding.getCryptoProvider(activity),
                computer.serverCert);
        decoder.setRenderTarget(target);
        AndroidAudioRenderer audio = new AndroidAudioRenderer(activity, prefs.enableAudioFx);
        acquireWifi(activity);
        final int token;
        synchronized (gate) {
            if (activity.isFinishing()) {
                releaseWifi();
                return false;
            }
            publishedToken = ++tokenSource;
            token = publishedToken;
            inputToken = 0;
            conn = created;
            refWidth = width;
            refHeight = height;
            currentAppId = app.getAppId();
            currentUuid = computer.uuid;
        }
        created.start(audio, decoder, new StreamListener(token, name));
        return true;
    }

    private void sendPosition(NvConnection current, float u, float v, int width, int height) {
        float su = u < 0f ? 0f : Math.min(u, 1f);
        float sv = v < 0f ? 0f : Math.min(v, 1f);
        int x = Math.round(su * (width - 1));
        int y = Math.round((1f - sv) * (height - 1));
        current.sendMousePosition((short) x, (short) y, (short) width, (short) height);
    }

    private void status(final String text) {
        MoonlightXr.nativeSetStatus(text != null ? text : "");
    }

    private void postIfCurrent(final int token, final Runnable action) {
        main.post(new Runnable() {
            @Override
            public void run() {
                synchronized (gate) {
                    if (token != publishedToken || activity == null || activity.isDestroyed()) {
                        return;
                    }
                }
                action.run();
            }
        });
    }

    private void stopIfCurrent(final int token) {
        NvConnection dying;
        synchronized (gate) {
            if (token != publishedToken) {
                return;
            }
            dying = detachLocked();
        }
        if (dying != null) {
            dying.stop();
        }
    }

    private void acquireWifi(Context context) {
        releaseWifi();
        WifiManager wifi = (WifiManager) context.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        if (wifi == null) {
            return;
        }
        try {
            highPerfWifiLock = wifi.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "Moonlight VR");
            highPerfWifiLock.setReferenceCounted(false);
            highPerfWifiLock.acquire();
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                lowLatencyWifiLock = wifi.createWifiLock(WifiManager.WIFI_MODE_FULL_LOW_LATENCY, "Moonlight VR Low Latency");
                lowLatencyWifiLock.setReferenceCounted(false);
                lowLatencyWifiLock.acquire();
            }
        } catch (SecurityException e) {
            LimeLog.warning("VR wifi lock was not granted");
        } catch (RuntimeException e) {
            LimeLog.warning("VR wifi lock failed: " + e.getMessage());
        }
    }

    private void releaseWifi() {
        releaseLock(highPerfWifiLock);
        releaseLock(lowLatencyWifiLock);
        highPerfWifiLock = null;
        lowLatencyWifiLock = null;
    }

    private static void releaseLock(WifiManager.WifiLock lock) {
        if (lock != null && lock.isHeld()) {
            try {
                lock.release();
            } catch (RuntimeException ignored) {
            }
        }
    }

    private void allowInput(int token) {
        synchronized (gate) {
            if (token == publishedToken) {
                inputToken = token;
            }
        }
    }

    private final class StreamListener implements NvConnectionListener {
        private final int token;
        private final String appName;

        StreamListener(int token, String appName) {
            this.token = token;
            this.appName = appName;
        }

        @Override
        public void stageStarting(String stage) {
            if (isCurrent(token)) {
                status(stage != null ? stage : appName);
            }
        }

        @Override
        public void stageComplete(String stage) {
        }

        @Override
        public void stageFailed(final String stage, int portFlags, final int errorCode) {
            postIfCurrent(token, new Runnable() {
                @Override
                public void run() {
                    String label = stage != null ? stage : appName;
                    status(label + " failed (" + errorCode + ")");
                    stopIfCurrent(token);
                }
            });
        }

        @Override
        public void connectionStarted() {
            allowInput(token);
            if (isCurrent(token)) {
                status(appName);
            }
        }

        @Override
        public void connectionTerminated(final int errorCode) {
            postIfCurrent(token, new Runnable() {
                @Override
                public void run() {
                    status(errorCode == 0 ? "Stream ended" : "Lost connection");
                    stopIfCurrent(token);
                }
            });
        }

        @Override
        public void connectionStatusUpdate(int connectionStatus) {
        }

        @Override
        public void displayMessage(final String message) {
            if (message != null && isCurrent(token)) {
                status(message);
            }
        }

        @Override
        public void displayTransientMessage(final String message) {
            if (message != null && isCurrent(token)) {
                status(message);
            }
        }

        @Override
        public void rumble(short controllerNumber, short lowFreqMotor, short highFreqMotor) {
        }

        @Override
        public void rumbleTriggers(short controllerNumber, short leftTrigger, short rightTrigger) {
        }

        @Override
        public void setHdrMode(boolean enabled, byte[] hdrMetadata) {
        }

        @Override
        public void setMotionEventState(short controllerNumber, byte motionType, short reportRateHz) {
        }

        @Override
        public void setControllerLED(short controllerNumber, byte r, byte g, byte b) {
        }
    }
}
