package android.text;

import java.util.Iterator;

public final class TextUtils {
    public enum TruncateAt { START, MIDDLE, END, MARQUEE, END_SMALL }
    private TextUtils() {}

    public static boolean isEmpty(CharSequence value) {
        return value == null || value.length() == 0;
    }

    public static boolean equals(CharSequence first, CharSequence second) {
        if (first == second) return true;
        if (first == null || second == null || first.length() != second.length())
            return false;
        for (int index = 0; index < first.length(); index++) {
            if (first.charAt(index) != second.charAt(index)) return false;
        }
        return true;
    }
    public static int indexOf(CharSequence text, char character) {
        return indexOf(text, character, 0);
    }
    public static int indexOf(CharSequence text, char character, int start) {
        if (text == null) return -1;
        for (int index = Math.max(0, start); index < text.length(); index++)
            if (text.charAt(index) == character) return index;
        return -1;
    }

    public static String join(CharSequence delimiter, Object[] values) {
        StringBuilder result = new StringBuilder();
        for (Object value : values) {
            if (result.length() != 0) result.append(delimiter);
            result.append(value);
        }
        return result.toString();
    }

    public static String join(CharSequence delimiter, Iterable<?> values) {
        StringBuilder result = new StringBuilder();
        Iterator<?> iterator = values.iterator();
        while (iterator.hasNext()) {
            if (result.length() != 0) result.append(delimiter);
            result.append(iterator.next());
        }
        return result.toString();
    }
}
