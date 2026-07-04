package android.content;

public class ClipDescription {
    private final CharSequence label;
    private final String[] mimeTypes;
    public ClipDescription(CharSequence label, String[] mimeTypes) {
        this.label = label;
        this.mimeTypes = mimeTypes == null ? new String[0] : mimeTypes.clone();
    }
    public CharSequence getLabel() { return label; }
    public int getMimeTypeCount() { return mimeTypes.length; }
    public String getMimeType(int index) { return mimeTypes[index]; }
    public boolean hasMimeType(String type) {
        for (String value : mimeTypes) if (value.equals(type)) return true;
        return false;
    }
}
