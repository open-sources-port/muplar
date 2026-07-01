package android.os;

import android.media.AudioAttributes;

public class Vibrator {
    public boolean hasVibrator() { return false; }
    public boolean areAllPrimitivesSupported(int... primitiveIds) { return false; }
    public void vibrate(VibrationEffect effect) {}
    public void vibrate(VibrationEffect effect, AudioAttributes attributes) {}
    public void cancel() {}
}
