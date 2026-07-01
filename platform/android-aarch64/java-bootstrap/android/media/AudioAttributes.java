package android.media;

public final class AudioAttributes {
    public static final class Builder {
        public Builder setUsage(int usage) { return this; }
        public Builder setContentType(int contentType) { return this; }
        public AudioAttributes build() { return new AudioAttributes(); }
    }
}
