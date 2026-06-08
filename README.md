# Moonlight Android – XR / Meta Quest Experimental Fork

**This fork was originally created to fix crashes on Meta Quest devices running v76+ firmware** (caused by Meta removing the GameManager component — see "Quest Firmware Compatibility" section below).

It has since been extended with **multi-window and multi-process support** to enable simultaneous active streaming connections to multiple PCs (the native library only supports one connection per process).

**⚠️ This remains an experimental fork targeting XR devices (primarily Meta Quest headsets).** It is not intended as a general-purpose replacement for upstream Moonlight Android.

[![AppVeyor Build Status](https://ci.appveyor.com/api/projects/status/232a8tadrrn8jv0k/branch/master?svg=true)](https://ci.appveyor.com/project/cgutman/moonlight-android/branch/master)
[![Translation Status](https://hosted.weblate.org/widgets/moonlight/-/moonlight-android/svg-badge.svg)](https://hosted.weblate.org/projects/moonlight/moonlight-android/)

---

## What’s Different in This Fork

Upstream Moonlight Android (and the underlying native `moonlight-common-c` library) only supports **one active streaming connection per process**. This works fine on phones and tablets, but is very limiting on XR devices where you often want to:

- Stream from two (or more) gaming PCs at the same time
- Keep one stream running in a volumetric / multi-window while using the PC list or AppView in another
- Take advantage of Quest’s multi-window / freeform / virtual desktop features

### Key Changes

- **Separate process slots for streaming**  
  The streaming activity now lives in dedicated processes (`:stream`, `:stream2`, `:stream3`, `:stream4`).  
  Each process loads its own copy of the native library, giving each its own independent connection state. This is the same technique used by app cloners and is currently the only practical way to get true simultaneous live streams.

- **Game2 / Game3 / Game4 activities**  
  Thin subclasses (`Game2.java`, etc.) are declared in the manifest with distinct `android:process` values. When you choose “Start in new window” / “Resume in new window”, the launcher rotates through these classes so each new stream gets its own process.

- **Long-press / context menu support for new windows**  
  - Long-press (hold) a PC in the PC list → context menu appears with **“Resume in new window”** (when a game is running) and other multi-window options.
  - Inside an AppView (after clicking a PC), long-press an app or use the context menu items **“Start in new window”**, **“Resume in new window”**, **“Quit Current Game and Start in new window”**.
  - These paths deliberately launch into a fresh process slot instead of replacing the existing stream.

- **VR / Quest-specific considerations**  
  The fork has been developed and tested primarily on Meta Quest devices (including volumetric / 3D multi-window environments). Some UI elements (context menus on long press, window focus behavior, etc.) behave differently in the Quest shell than on a normal phone or tablet. The code contains defensive guards for stale list positions, cross-process service binding (USB driver, etc.), and process-aware takeover logic.

- **Controller pointer as mouse (Quest Touch / laser pointer)**  
  New optional setting **“Capture controller pointer as mouse”** (in the Input Settings section when configuring a stream; disabled by default).  
  On Meta Quest headsets, this lets the Touch controller’s laser pointer / virtual cursor continuously drive the remote PC’s mouse position. Previously the mouse would only warp to the pointer location when you pressed the trigger. This makes desktop-style interaction and precise pointing much more natural while streaming.

- **Mouse edge release for multi-window use**  
  New setting **“Release mouse capture at window edge”** (enabled by default). When using a captured USB/Bluetooth mouse, pushing the pointer past the edge of the Moonlight window automatically releases pointer capture. This lets you move the system cursor out of the stream window to interact with other volumetric / freeform windows or Quest system UI without having to use the grab hotkey. The virtual cursor is tracked locally so edge detection works even while relative (captured) input is active.

- **Absolute mouse passthrough (no pointer capture)**  
  New optional setting **“Absolute mouse passthrough (no pointer capture)”** (in the Input Settings section when configuring a stream; disabled by default).  
  When enabled, the local Android cursor stays visible at all times and directly controls the remote PC’s mouse when over the stream window. Mouse movements and clicks are forwarded as absolute input. Pointer capture is never used for the mouse, so there is no grabbing, releasing, edge detection, or virtual cursor tracking. Simply move the cursor out of the Moonlight window to use other Quest windows or the system UI. This is the recommended mode for desktop and productivity use inside Quest’s multi-window / freeform environment.

- **v76+ firmware crash fix (original reason for the fork)**  
  Starting with Meta Quest firmware v76+, Meta removed yet another internal Android OS component — the GameManager (gaming mode / performance service, historically reachable via `com.oculus.gamemanager.GameManager` or through reflection on system services).  

  Upstream Moonlight called into this to call `setGameModeStatus()` (and similar) so the headset would know the app was actively gaming. This allowed Quest to apply better CPU/GPU scheduling, thermal headroom, and "in-game" power profiles.

  After the removal, any attempt to obtain the service or invoke the methods (even defensively via reflection) would throw and crash the app — typically on stream launch or very early in `Game` activity creation.

  The fix (commit `2276a02f` titled "Removed callings to GameManager") excises the integration entirely:

  - In `UiHelper.java` the five notification methods are reduced to no-ops:
    ```java
    public static void notifyStreamConnecting(Context context) { /* No-op */ }
    public static void notifyStreamConnected(Context context) { /* No-op */ }
    public static void notifyStreamEnteringPiP(Context context) { /* No-op */ }
    public static void notifyStreamExitingPiP(Context context) { /* No-op */ }
    public static void notifyStreamEnded(Context context) { /* No-op */ }
    ```
    A comment documents the removal:
    ```java
    // Removed setGameModeStatus() and its calls from this class
    ```

  - The call sites inside `Game.java` (in `onPictureInPictureRequested`, `stopConnection`, `surfaceChanged`, `connectionStarted`, etc.) still invoke the no-op helpers so the call sites didn't have to be littered with conditionals:
    ```java
    UiHelper.notifyStreamConnected(Game.this);
    UiHelper.notifyStreamEnded(this);
    UiHelper.notifyStreamEnteringPiP(this);
    ...
    ```

  This was the original motivation for the fork. The multi-window / separate-process work for simultaneous PC streaming was layered on top afterward.

  Note that "debug" builds (`com.limelight.debug`) and any "unofficial"/"stable" side-loads may have different package IDs, which can affect how the Quest shell and library permissions treat them.

- **Upstream behavior is largely preserved for single-stream use**  
  Normal short clicks, the main PC list, AppView, etc. continue to work as before. The multi-window features are opt-in via long press or the explicit “in new window” menu items.

---

## Limitations & Known Issues

- Only **one active native stream per process**. The four process slots give you a practical maximum of ~4 truly simultaneous live connections.
- USB controller passthrough is only fully reliable in the primary stream process (`:stream`). Secondary slots fall back gracefully.
- This is **experimental**. You may encounter crashes, frozen windows, focus issues, or other quirks, especially when rapidly opening/closing windows or when the Quest shell is managing many volumetric windows.
- Debug and release (“stable”) builds may behave slightly differently (different package names, ProGuard, etc.). Use the debug build (`com.limelight.debug`) when reporting issues.
- Some features that assume a single-process world (certain singletons, shortcut handling, USB driver state) have been made more defensive but are not perfect.

If you just want normal single-PC streaming on a phone or tablet, you are probably better off with the official upstream build.

---

## Building

The build process is the same as upstream:

* Install Android Studio and the Android NDK
* Run `git submodule update --init --recursive` from within `moonlight-android/`
* In `moonlight-android/`, create a file called `local.properties`. Add an `ndk.dir=` property and point it at your NDK installation.
* Build the APK using Android Studio or Gradle (`./gradlew assembleDebug` for a debug build with the `.debug` suffix).

After building, install with:

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
# or the release variant if you built it
```

Because multiple processes are declared, you may want to grant the app “Display over other apps” / “Appear on top” permission on Quest for the best multi-window experience.

---

## Usage Tips for Simultaneous Connections

1. Make sure you have at least two paired PCs visible in the PC list.
2. Long-press (hold) a PC that has a running game → choose **“Resume in new window”**.
3. Or enter the AppView for a PC and long-press an app (or use the context menu) and pick one of the “in new window” options.
4. Each new stream should open in its own volumetric window / task and run independently.

You can also combine this with Quest’s native multi-window / virtual desktop features.

If a second stream fails to start or the first one freezes, check the logs (see below) and make sure you’re using a recent build that contains the `Game2`/`Game3`/`Game4` + process declarations.

---

## Logging & Debugging

When reporting issues, please include logs from the relevant process(es). The app logs under the tag `com.limelight` (or `com.limelight.debug` / `com.limelight.unofficial` depending on the build you’re running).

Example command (while the device is connected):

```bash
adb logcat -v threadtime | grep -E "(com\.limelight|Game|NvConnection|MoonBridge|streamActive|takingOver)" > moonlight.log
```

You can also run a background capture:

```bash
adb logcat -v threadtime > /tmp/moonlight_$(date +%s).log 2>&1 &
```

Mention whether you are using the debug or a “stable”/unofficial build.

---

## Authors & Upstream

This fork is based on the excellent work of the upstream Moonlight Android team:

* [Cameron Gutman](https://github.com/cgutman)  
* [Diego Waxemberg](https://github.com/dwaxemberg)  
* [Aaron Neyer](https://github.com/Aaronneyer)  
* [Andrew Hennessy](https://github.com/yetanothername)

Moonlight is the work of students at [Case Western](http://case.edu) and was started as a project at [MHacks](http://mhacks.org).

The XR / multi-process / multi-window changes in this fork were developed to make simultaneous PC streaming practical on Meta Quest and similar devices.

Upstream project: https://github.com/moonlight-stream/moonlight-android  
Upstream website: https://moonlight-stream.org

You can follow general Moonlight development on the [Discord server](https://moonlight-stream.org/discord).

---

## License

Same as upstream (GNU GPLv3 or later – see the original repository for details).
