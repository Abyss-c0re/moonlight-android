package com.limelight.vr;

import android.app.Activity;
import android.app.Service;
import android.content.ComponentName;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Bundle;
import android.os.IBinder;
import android.view.View;
import android.view.WindowManager;
import android.widget.Toast;

import com.limelight.R;
import com.limelight.binding.PlatformBinding;
import com.limelight.computers.ComputerManagerListener;
import com.limelight.computers.ComputerManagerService;
import com.limelight.nvstream.http.ComputerDetails;
import com.limelight.nvstream.http.NvApp;
import com.limelight.nvstream.http.NvHTTP;
import com.limelight.nvstream.http.PairingManager;

import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.LinkedList;

/**
 * OpenXR activity started from the PC-list VR button. Inside the headset the
 * menu lists paired PCs and their apps. Choosing an app streams it onto the
 * movable screens. Exit to app stops that stream and returns to the 2D app.
 * System overlays pause the activity without tearing the session down.
 */
public class VrActivity extends Activity {
    private static final String HINT = "Grip moves  |  A/X spawns  |  menu shows this panel";

    private final VrStreamHost streamHost = new VrStreamHost();

    private boolean started;
    private boolean xrReady;
    private boolean inForeground;
    private boolean bound;
    private boolean runningPolling;
    private boolean showingApps;
    private boolean announcedEmpty;
    private int libraryGeneration;
    private String publishedKey = "";
    private ComputerManagerService.ComputerManagerBinder managerBinder;
    private final ArrayList<ComputerDetails> computers = new ArrayList<>();
    private final ArrayList<ComputerDetails> shownPcs = new ArrayList<>();
    private final ArrayList<NvApp> shownApps = new ArrayList<>();
    private ComputerDetails listedPc;

    private final ServiceConnection serviceConnection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName className, IBinder binder) {
            final ComputerManagerService.ComputerManagerBinder localBinder =
                    (ComputerManagerService.ComputerManagerBinder) binder;
            new Thread(new Runnable() {
                @Override
                public void run() {
                    localBinder.waitForReady();
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            if (!bound || isFinishing() || isDestroyed()) {
                                return;
                            }
                            managerBinder = localBinder;
                            startComputerUpdates();
                        }
                    });
                }
            }, "vr-cms").start();
        }

        @Override
        public void onServiceDisconnected(ComponentName className) {
            managerBinder = null;
            runningPolling = false;
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        streamHost.attach(this);
        inForeground = true;
        setContentView(new View(this));
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        bindService(new Intent(this, ComputerManagerService.class), serviceConnection, Service.BIND_AUTO_CREATE);
        bound = true;
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        MoonlightXr.nativeRequestExit();
    }

    @Override
    protected void onResume() {
        super.onResume();
        inForeground = true;
        if (!started) {
            started = true;
            xrReady = MoonlightXr.nativeStart(this);
            if (!xrReady) {
                Toast.makeText(this, R.string.vr_unavailable, Toast.LENGTH_LONG).show();
                finish();
                return;
            }
            MoonlightXr.nativeSetStatus(HINT);
        }
        startComputerUpdates();
    }

    @Override
    protected void onPause() {
        // Quest and Pico system UI pause this activity. The OpenXR session
        // stays alive and resumes when the runtime returns to READY.
        inForeground = false;
        stopComputerUpdates();
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        streamHost.stop();
        if (runningPolling) {
            stopComputerUpdates();
        }
        if (bound) {
            unbindService(serviceConnection);
            bound = false;
            managerBinder = null;
        }
        MoonlightXr.nativeShutdown();
        super.onDestroy();
    }

    @SuppressWarnings("unused")
    public void onSessionEnded(String reason) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (!isFinishing()) {
                    finish();
                }
            }
        });
    }

    @SuppressWarnings("unused")
    public void onPointer(int phase, float u, float v, int button) {
        streamHost.onPointer(phase, u, v, button);
    }

    public void onVideoTexture(int texId) {
        streamHost.attachTexture(texId);
    }

    public int onVideoFrame(float[] matrix) {
        return streamHost.pullFrame(matrix);
    }

    public void onVideoRelease() {
        streamHost.releaseSurface();
    }

    public void onLibraryPick(final int action, final int id) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (isFinishing() || isDestroyed()) {
                    return;
                }
                handlePick(action, id);
            }
        });
    }

    private void startComputerUpdates() {
        if (managerBinder == null || runningPolling || !inForeground || !xrReady) {
            return;
        }
        managerBinder.startPolling(new ComputerManagerListener() {
            @Override
            public void notifyComputerUpdated(ComputerDetails details) {
                if (details == null || details.uuid == null) {
                    return;
                }
                final ComputerDetails copy = new ComputerDetails();
                copy.update(details);
                if (details.activeAddress != null && details.activeAddress.address != null) {
                    copy.activeAddress = new ComputerDetails.AddressTuple(
                            details.activeAddress.address, details.activeAddress.port);
                }
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        if (isFinishing() || isDestroyed()) {
                            return;
                        }
                        upsert(copy);
                    }
                });
            }
        });
        runningPolling = true;
    }

    private void stopComputerUpdates() {
        if (managerBinder != null && runningPolling) {
            managerBinder.stopPolling();
            runningPolling = false;
        }
    }

    private void upsert(ComputerDetails details) {
        if (details.uuid == null) {
            return;
        }
        for (int i = 0; i < computers.size(); i++) {
            if (details.uuid.equals(computers.get(i).uuid)) {
                computers.set(i, details);
                if (!showingApps) {
                    publishPcs();
                }
                return;
            }
        }
        computers.add(details);
        if (!showingApps) {
            publishPcs();
        }
    }

    private void publishPcs() {
        shownPcs.clear();
        ArrayList<String> labels = new ArrayList<>();
        ArrayList<Integer> kinds = new ArrayList<>();
        ArrayList<Integer> ids = new ArrayList<>();
        for (ComputerDetails computer : computers) {
            if (shownPcs.size() >= 12) {
                break;
            }
            if (computer.state == ComputerDetails.State.ONLINE &&
                    computer.pairState == PairingManager.PairState.PAIRED &&
                    computer.activeAddress != null) {
                int index = shownPcs.size();
                shownPcs.add(computer);
                labels.add(computer.name != null ? computer.name : "PC");
                kinds.add(MoonlightXr.ACT_PC);
                ids.add(index);
            }
        }
        pushLibrary(labels, kinds, ids);
        if (shownPcs.isEmpty()) {
            if (!announcedEmpty) {
                if (!streamHost.isActive()) {
                    MoonlightXr.nativeSetStatus("No paired PCs online");
                }
                announcedEmpty = true;
            }
            return;
        }
        if (announcedEmpty) {
            announcedEmpty = false;
            if (!streamHost.isActive()) {
                MoonlightXr.nativeSetStatus(HINT);
            }
        }
    }

    private void pushLibrary(ArrayList<String> labels, ArrayList<Integer> kinds, ArrayList<Integer> ids) {
        StringBuilder key = new StringBuilder();
        for (int i = 0; i < labels.size(); i++) {
            key.append(kinds.get(i)).append(':').append(ids.get(i)).append(':').append(labels.get(i)).append('\n');
        }
        String next = key.toString();
        if (next.equals(publishedKey)) {
            return;
        }
        publishedKey = next;
        String[] labelArray = labels.toArray(new String[0]);
        int[] kindArray = new int[kinds.size()];
        int[] idArray = new int[ids.size()];
        for (int i = 0; i < kinds.size(); i++) {
            kindArray[i] = kinds.get(i);
            idArray[i] = ids.get(i);
        }
        MoonlightXr.nativeSetLibrary(labelArray, kindArray, idArray);
    }

    private void handlePick(int action, int id) {
        if (action == MoonlightXr.ACT_PC) {
            if (id < 0 || id >= shownPcs.size()) {
                return;
            }
            loadApps(shownPcs.get(id));
        } else if (action == MoonlightXr.ACT_APP) {
            for (NvApp app : shownApps) {
                if (app.getAppId() == id) {
                    openApp(app);
                    return;
                }
            }
        } else if (action == MoonlightXr.ACT_BACK) {
            libraryGeneration++;
            showingApps = false;
            shownApps.clear();
            publishedKey = "*";
            publishPcs();
            if (!shownPcs.isEmpty() && !streamHost.isActive()) {
                MoonlightXr.nativeSetStatus(HINT);
            }
        }
    }

    private void loadApps(ComputerDetails computer) {
        if (computer.activeAddress == null || computer.activeAddress.address == null) {
            MoonlightXr.nativeSetStatus("That PC is offline");
            return;
        }
        listedPc = computer;
        if (managerBinder == null) {
            MoonlightXr.nativeSetStatus("App list failed");
            return;
        }
        final int generation = ++libraryGeneration;
        final ComputerDetails.AddressTuple address = new ComputerDetails.AddressTuple(
                computer.activeAddress.address, computer.activeAddress.port);
        final int httpsPort = computer.httpsPort;
        final X509Certificate cert = computer.serverCert;
        final String uniqueId = managerBinder.getUniqueId();
        final String pcName = computer.name != null ? computer.name : "PC";
        showingApps = true;
        MoonlightXr.nativeSetStatus("Loading apps");
        new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    NvHTTP http = new NvHTTP(address, httpsPort, uniqueId, cert,
                            PlatformBinding.getCryptoProvider(VrActivity.this));
                    final LinkedList<NvApp> apps = http.getAppList();
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            if (generation != libraryGeneration || isFinishing() || isDestroyed()) {
                                return;
                            }
                            showApps(pcName, apps);
                        }
                    });
                } catch (Exception e) {
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            if (generation != libraryGeneration || isFinishing() || isDestroyed()) {
                                return;
                            }
                            showingApps = false;
                            MoonlightXr.nativeSetStatus("App list failed");
                        }
                    });
                }
            }
        }, "vr-applist").start();
    }

    private void showApps(String pcName, LinkedList<NvApp> apps) {
        showingApps = true;
        shownApps.clear();
        ArrayList<String> labels = new ArrayList<>();
        ArrayList<Integer> kinds = new ArrayList<>();
        ArrayList<Integer> ids = new ArrayList<>();
        labels.add("Back");
        kinds.add(MoonlightXr.ACT_BACK);
        ids.add(0);
        if (apps != null) {
            for (NvApp app : apps) {
                if (labels.size() >= 12) {
                    break;
                }
                if (app == null || !app.isInitialized()) {
                    continue;
                }
                shownApps.add(app);
                String name = app.getAppName();
                labels.add(name != null && name.length() > 0 ? name : "App");
                kinds.add(MoonlightXr.ACT_APP);
                ids.add(app.getAppId());
            }
        }
        publishedKey = "";
        pushLibrary(labels, kinds, ids);
        MoonlightXr.nativeSetStatus(pcName);
    }

    private void openApp(NvApp app) {
        if (listedPc == null || managerBinder == null) {
            MoonlightXr.nativeSetStatus("Stream failed");
            return;
        }
        streamHost.open(this, listedPc, app, managerBinder.getUniqueId());
    }
}
