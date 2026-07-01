package android.os;

public class VibrationEffect {
    public static VibrationEffect createPredefined(int effectId) {
        return new VibrationEffect();
    }
    public static Composition startComposition() { return new Composition(); }
    public static final class Composition {
        public Composition addPrimitive(int primitiveId, float scale) { return this; }
        public Composition addPrimitive(int primitiveId, float scale,
                int delay) { return this; }
        public VibrationEffect compose() { return new VibrationEffect(); }
    }
}
