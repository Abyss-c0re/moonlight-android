# Moonlight Android – headset & keyboard tweaks fork

**This fork was originally created to fix crashes on Meta Quest devices running v76+ firmware** (caused by Meta removing the GameManager component — see the firmware notes below).

It has since been extended with **multi-window and multi-process support**, plus input tweaks that help on **VR headsets** and **hardware keyboard devices** (for example the Unihertz Titan 2).

**This is an experimental fork.** It is not intended as a general-purpose replacement for upstream Moonlight Android. Use it when you need the multi-window stream slots or the headset/keyboard-oriented options described here.

[![AppVeyor Build Status](https://ci.appveyor.com/api/projects/status/232a8tadrrn8jv0k/branch/master?svg=true)](https://ci.appveyor.com/project/cgutman/moonlight-android/branch/master)
[![Translation Status](https://hosted.weblate.org/widgets/moonlight/-/moonlight-android/svg-badge.svg)](https://hosted.weblate.org/projects/moonlight/moonlight-android/)

---

## What’s Different in This Fork

Upstream Moonlight Android (and the underlying native `moonlight-common-c` library) only supports **one active streaming connection per process**. That is fine on phones and tablets, but limiting when you want multiple streams or multi-window desktop use (common on headsets and some productivity setups).

Typical cases:

- Stream from two (or more) gaming PCs at the same time
- Keep one stream running in a volumetric / multi-window while using the PC list or AppView in another
- Use Quest multi-window / freeform / virtual desktop features

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

- **VR headset considerations**  
  Several options and defensive fixes target Meta Quest and similar headsets (volumetric / 3D multi-window, laser pointer mouse, absolute mouse passthrough). UI details (context menus on long press, window focus, etc.) can differ from a normal phone or tablet. The code guards stale list positions, cross-process service binding (USB driver, etc.), and process-aware takeover.

- **Hardware keyboard devices (e.g. Titan 2)**  
  Optional input settings help when a physical keyboard is attached or built in (character composition, capture behavior). See Input Settings while configuring a stream.

- **Controller pointer as mouse (Quest Touch / laser pointer)**  
  Optional setting **“Capture controller pointer as mouse”** (Input Settings; disabled by default).  
  On Meta Quest headsets, the Touch controller laser pointer / virtual cursor continuously drives the remote PC’s mouse position instead of only jumping when the trigger is pressed.

- **Absolute mouse passthrough (no pointer capture)**  
  Optional setting **“Absolute mouse passthrough (no pointer capture)”** (Input Settings; disabled by default).  
  When enabled, the local Android cursor stays visible and controls the remote PC’s mouse over the stream window via absolute input. Pointer capture is not used, so you can move the cursor out of the Moonlight window to other Quest windows or the system UI. Useful for multi-window desktop use on headsets.

- **v76+ firmware crash fix (original reason for the fork)**  
  Starting with Meta Quest firmware v76+, Meta removed the GameManager component (gaming mode / performance service, historically `com.oculus.gamemanager.GameManager` or reflection on system services).

  Upstream Moonlight called `setGameModeStatus()` (and similar) so the headset could apply gaming CPU/GPU scheduling and power profiles.

  After the removal, obtaining the service or invoking those methods crashed the app — typically on stream launch or early in `Game` activity creation.

  The fix (commit `2276a02f`, "Removed callings to GameManager") removes the integration:

  - In `UiHelper.java` the five notification methods are no-ops:
    ```java
    public static void notifyStreamConnecting(Context context) { /* No-op */ }
    public static void notifyStreamConnected(Context context) { /* No-op */ }
    public static void notifyStreamEnteringPiP(Context context) { /* No-op */ }
    public static void notifyStreamExitingPiP(Context context) { /* No-op */ }
    public static void notifyStreamEnded(Context context) { /* No-op */ }
    ```
    Comment in tree:
    ```java
    // Removed setGameModeStatus() and its calls from this class
    ```

  - Call sites in `Game.java` still invoke the no-op helpers so they stay consistent:
    ```java
    UiHelper.notifyStreamConnected(Game.this);
    UiHelper.notifyStreamEnded(this);
    UiHelper.notifyStreamEnteringPiP(this);
    ...
    ```

  Multi-window / separate-process work was added after that baseline.

  Note that "debug" builds (`com.limelight.debug`) and unofficial side-loads may use different package IDs, which can affect Quest shell and library permissions.

- **Upstream behavior is largely preserved for single-stream use**  
  Normal short clicks, the main PC list, AppView, etc. continue to work as before. Multi-window features are opt-in via long press or the “in new window” menu items. Optional input tweaks default to off.

---

## Limitations & Known Issues

- Only **one active native stream per process**. The four process slots give a practical maximum of ~4 simultaneous live connections.
- USB controller passthrough is only fully reliable in the primary stream process (`:stream`). Secondary slots fall back gracefully.
- This is **experimental**. You may hit crashes, frozen windows, focus issues, or other quirks, especially when opening/closing windows quickly or when a headset shell manages many volumetric windows.
- Debug and release (“stable”) builds may differ (package names, ProGuard, etc.). Prefer the debug build (`com.limelight.debug`) when reporting issues.
- Some features that assume a single-process world (singletons, shortcut handling, USB driver state) are more defensive but not perfect.

If you only need normal single-PC streaming on a phone or tablet, the official upstream build is usually the better choice.

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

Multi-process / multi-window support and headset/keyboard-oriented options in this fork exist to make simultaneous PC streaming and hardware-keyboard use practical on devices such as Meta Quest and keyboard phones (e.g. Titan 2).

Upstream project: https://github.com/moonlight-stream/moonlight-android  
Upstream website: https://moonlight-stream.org

You can follow general Moonlight development on the [Discord server](https://moonlight-stream.org/discord).

---

## License

Same as upstream (GNU GPLv3 or later – see the original repository for details).
