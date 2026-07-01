package android.util;

public final class StatsEvent {
    private StatsEvent() {}
    public static Builder newBuilder() { return new Builder(); }
    public static final class Builder {
        public Builder setAtomId(int id) { return this; }
        public Builder writeInt(int value) { return this; }
        public Builder writeLong(long value) { return this; }
        public Builder writeString(String value) { return this; }
        public Builder writeBoolean(boolean value) { return this; }
        public Builder usePooledBuffer() { return this; }
        public StatsEvent build() { return new StatsEvent(); }
    }
}
