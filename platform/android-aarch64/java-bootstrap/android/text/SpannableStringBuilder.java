package android.text;

public class SpannableStringBuilder implements Editable {
    private final StringBuilder text;
    public SpannableStringBuilder() { text = new StringBuilder(); }
    public SpannableStringBuilder(CharSequence source) {
        text = new StringBuilder(source == null ? "" : source.toString());
    }
    @Override public SpannableStringBuilder append(CharSequence value) {
        text.append(value); return this;
    }
    @Override public SpannableStringBuilder append(CharSequence value,
            int start, int end) { text.append(value, start, end); return this; }
    @Override public SpannableStringBuilder append(char value) {
        text.append(value); return this;
    }
    @Override public void clear() { text.setLength(0); }
    @Override public void setSpan(Object what, int start, int end, int flags) {}
    @Override public void removeSpan(Object what) {}
    @Override public int length() { return text.length(); }
    @Override public char charAt(int index) { return text.charAt(index); }
    @Override public CharSequence subSequence(int start, int end) {
        return text.subSequence(start, end);
    }
    @Override public String toString() { return text.toString(); }
}
