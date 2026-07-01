package android.text;

public final class Selection {
    private Selection() {}
    public static void setSelection(Spannable text, int index) {}
    public static void setSelection(Spannable text, int start, int stop) {}
    public static int getSelectionStart(CharSequence text) { return 0; }
    public static int getSelectionEnd(CharSequence text) { return 0; }
}
