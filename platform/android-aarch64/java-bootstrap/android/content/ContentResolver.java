package android.content;

public class ContentResolver {
    private final Context context;
    public ContentResolver(Context context) { this.context = context; }
    public Context getContext() { return context; }
}
