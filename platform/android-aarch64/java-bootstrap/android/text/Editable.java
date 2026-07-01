package android.text;

public interface Editable extends Spannable, Appendable {
    Editable append(CharSequence text);
    Editable append(CharSequence text, int start, int end);
    Editable append(char text);
    void clear();
}
