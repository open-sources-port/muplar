package android.app.prediction;

import android.os.UserHandle;

public final class AppTarget {
    private final AppTargetId id;
    private final String packageName;
    private final UserHandle user;
    private final String className;
    private final int rank;

    private AppTarget(Builder builder) {
        id = builder.id;
        packageName = builder.packageName;
        user = builder.user;
        className = builder.className;
        rank = builder.rank;
    }
    public AppTargetId getId() { return id; }
    public String getPackageName() { return packageName; }
    public UserHandle getUser() { return user; }
    public String getClassName() { return className; }
    public int getRank() { return rank; }

    public static final class Builder {
        private final AppTargetId id;
        private final String packageName;
        private final UserHandle user;
        private String className;
        private int rank;
        public Builder(AppTargetId id, String packageName, UserHandle user) {
            this.id = id;
            this.packageName = packageName;
            this.user = user;
        }
        public Builder setClassName(String value) { className = value; return this; }
        public Builder setRank(int value) { rank = value; return this; }
        public AppTarget build() { return new AppTarget(this); }
    }
}
