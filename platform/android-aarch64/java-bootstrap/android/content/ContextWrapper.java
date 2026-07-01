package android.content;

public class ContextWrapper extends Context {
    private Context base;
    public ContextWrapper(Context base) { this.base = base; }
    protected void attachBaseContext(Context base) { this.base = base; }
    public Context getBaseContext() { return base; }
    @Override public Context getApplicationContext() {
        return base == null ? super.getApplicationContext()
            : base.getApplicationContext();
    }
    @Override public String getPackageName() {
        return base == null ? super.getPackageName() : base.getPackageName();
    }
}
