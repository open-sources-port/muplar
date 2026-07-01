package android.app;

import android.os.Parcel;
import android.os.Parcelable;

public final class Person implements Parcelable {
    private final CharSequence name;
    private final String uri;
    private final String key;
    private final boolean bot;
    private final boolean important;
    private Person(Builder builder) {
        name = builder.name; uri = builder.uri; key = builder.key;
        bot = builder.bot; important = builder.important;
    }
    public CharSequence getName() { return name; }
    public String getUri() { return uri; }
    public String getKey() { return key; }
    public boolean isBot() { return bot; }
    public boolean isImportant() { return important; }
    public Builder toBuilder() {
        return new Builder().setName(name).setUri(uri).setKey(key)
            .setBot(bot).setImportant(important);
    }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel destination, int flags) {
        destination.writeString(name == null ? null : name.toString());
        destination.writeString(uri); destination.writeString(key);
        destination.writeBoolean(bot); destination.writeBoolean(important);
    }
    public static final class Builder {
        private CharSequence name;
        private String uri;
        private String key;
        private boolean bot;
        private boolean important;
        public Builder setName(CharSequence value) { name = value; return this; }
        public Builder setUri(String value) { uri = value; return this; }
        public Builder setKey(String value) { key = value; return this; }
        public Builder setBot(boolean value) { bot = value; return this; }
        public Builder setImportant(boolean value) { important = value; return this; }
        public Person build() { return new Person(this); }
    }
}
