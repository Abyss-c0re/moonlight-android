package com.limelight;

/**
 * Thin subclass so we can declare it in AndroidManifest.xml with a separate
 * android:process=":stream2". This gives the Game a completely independent
 * process (and thus independent copy of the native moonlight-core library
 * and all its global/static state).
 *
 * This allows true simultaneous streaming to multiple PCs without app cloning.
 */
public class Game2 extends Game {
}
