package android.text;

public interface TextWatcher {
    void beforeTextChanged(CharSequence text, int start, int count, int after);
    void onTextChanged(CharSequence text, int start, int before, int count);
    void afterTextChanged(Editable text);
}
