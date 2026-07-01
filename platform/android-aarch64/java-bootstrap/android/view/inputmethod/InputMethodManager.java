package android.view.inputmethod;

import android.view.View;

public class InputMethodManager {
    public static final int SHOW_IMPLICIT = 1;
    public boolean showSoftInput(View view, int flags) { return false; }
    public boolean hideSoftInputFromWindow(Object token, int flags) { return true; }
}
