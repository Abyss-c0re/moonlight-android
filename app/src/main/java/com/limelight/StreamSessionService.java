package com.limelight;

import android.app.Activity;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;

import java.lang.ref.WeakReference;

/**
 * Keeps the stream process alive after the Game activity leaves the foreground.
 * One subclass per :stream process, because a service's process is fixed in the manifest.
 */
public class StreamSessionService extends Service {
    public interface Host {
        void onStreamPauseRequested();
        void onStreamResumeRequested();
        void onStreamStopRequested();
    }

    public static final String ACTION_SHOW = "com.limelight.action.STREAM_SHOW";
    public static final String ACTION_PAUSE = "com.limelight.action.STREAM_PAUSE";
    public static final String ACTION_RESUME = "com.limelight.action.STREAM_RESUME";
    public static final String ACTION_STOP = "com.limelight.action.STREAM_STOP";
    public static final String ACTION_DISMISS = "com.limelight.action.STREAM_DISMISS";

    public static final String EXTRA_TITLE = "title";
    public static final String EXTRA_PAUSED = "paused";
    public static final String EXTRA_ACTIVITY = "activity";

    private static final int NOTIFICATION_ID = 0x4d4c01;
    private static final String CHANNEL_ID = "moonlight_stream_keep";
    private static final int REQ_OPEN = 1;
    private static final int REQ_PAUSE = 2;
    private static final int REQ_RESUME = 3;
    private static final int REQ_STOP = 4;

    private static WeakReference<Host> hostRef = new WeakReference<>(null);

    private String lastTitle;
    private boolean lastPaused;
    private String lastActivity;

    public static void setHost(Host host) {
        hostRef = new WeakReference<>(host);
    }

    public static Host getHost() {
        return hostRef.get();
    }

    public static Class<? extends Service> serviceClassFor(Activity activity) {
        String name = activity.getClass().getSimpleName();
        if ("Game2".equals(name)) {
            return StreamSessionService2.class;
        }
        if ("Game3".equals(name)) {
            return StreamSessionService3.class;
        }
        if ("Game4".equals(name)) {
            return StreamSessionService4.class;
        }
        return StreamSessionService.class;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null) {
            stopSelf();
            return START_NOT_STICKY;
        }

        String action = intent.getAction();
        if (ACTION_DISMISS.equals(action)) {
            stopForeground(true);
            stopSelf();
            return START_NOT_STICKY;
        }

        remember(intent);

        if (ACTION_PAUSE.equals(action) || ACTION_RESUME.equals(action) || ACTION_STOP.equals(action)) {
            Host host = getHost();
            if (host == null) {
                stopForeground(true);
                stopSelf();
                return START_NOT_STICKY;
            }
            if (ACTION_PAUSE.equals(action)) {
                host.onStreamPauseRequested();
            }
            else if (ACTION_RESUME.equals(action)) {
                host.onStreamResumeRequested();
            }
            else {
                host.onStreamStopRequested();
                stopForeground(true);
                stopSelf();
            }
            return START_NOT_STICKY;
        }

        enterForeground(buildNotification());
        return START_NOT_STICKY;
    }

    private void remember(Intent intent) {
        if (intent.hasExtra(EXTRA_TITLE)) {
            lastTitle = intent.getStringExtra(EXTRA_TITLE);
        }
        if (intent.hasExtra(EXTRA_PAUSED)) {
            lastPaused = intent.getBooleanExtra(EXTRA_PAUSED, false);
        }
        if (intent.hasExtra(EXTRA_ACTIVITY)) {
            lastActivity = intent.getStringExtra(EXTRA_ACTIVITY);
        }
    }

    private void enterForeground(Notification notification) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
        }
        else {
            startForeground(NOTIFICATION_ID, notification);
        }
    }

    private Notification buildNotification() {
        ensureChannel();

        String title = lastTitle != null ? lastTitle : getString(R.string.stream_notification_title);
        String text = getString(lastPaused ? R.string.stream_notification_paused : R.string.stream_notification_playing);
        Notification.Builder builder = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? new Notification.Builder(this, CHANNEL_ID)
                : new Notification.Builder(this);

        builder.setSmallIcon(android.R.drawable.ic_media_play)
                .setContentTitle(title)
                .setContentText(text)
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .setCategory(Notification.CATEGORY_SERVICE)
                .setVisibility(Notification.VISIBILITY_PUBLIC)
                .setContentIntent(pendingActivity())
                .addAction(new Notification.Action(
                        lastPaused ? android.R.drawable.ic_media_play : android.R.drawable.ic_media_pause,
                        getString(lastPaused ? R.string.stream_notification_resume : R.string.stream_notification_pause),
                        pendingService(lastPaused ? ACTION_RESUME : ACTION_PAUSE, lastPaused ? REQ_RESUME : REQ_PAUSE)))
                .addAction(new Notification.Action(
                        android.R.drawable.ic_menu_close_clear_cancel,
                        getString(R.string.stream_notification_stop),
                        pendingService(ACTION_STOP, REQ_STOP)));

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            builder.setForegroundServiceBehavior(Notification.FOREGROUND_SERVICE_IMMEDIATE);
        }
        return builder.build();
    }

    private void ensureChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
            return;
        }
        NotificationManager manager = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        if (manager == null || manager.getNotificationChannel(CHANNEL_ID) != null) {
            return;
        }
        NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID,
                getString(R.string.stream_notification_channel),
                NotificationManager.IMPORTANCE_HIGH);
        channel.setDescription(getString(R.string.stream_notification_playing));
        manager.createNotificationChannel(channel);
    }

    private int pendingFlags() {
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            flags |= PendingIntent.FLAG_IMMUTABLE;
        }
        return flags;
    }

    private PendingIntent pendingService(String action, int requestCode) {
        Intent intent = new Intent(this, getClass());
        intent.setAction(action);
        return PendingIntent.getService(this, requestCode, intent, pendingFlags());
    }

    private PendingIntent pendingActivity() {
        Intent open = new Intent();
        String className = lastActivity != null ? lastActivity : "com.limelight.Game";
        open.setComponent(new ComponentName(this, className));
        open.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK
                | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT
                | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        return PendingIntent.getActivity(this, REQ_OPEN, open, pendingFlags());
    }
}
