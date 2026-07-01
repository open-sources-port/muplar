package android.os;

import java.util.Arrays;
import java.util.Locale;

public final class LocaleList {
    private static final LocaleList EMPTY = new LocaleList();
    private final Locale[] locales;
    public LocaleList(Locale... locales) {
        this.locales = locales == null ? new Locale[0] : locales.clone();
    }
    public Locale get(int index) { return locales[index]; }
    public int size() { return locales.length; }
    public boolean isEmpty() { return locales.length == 0; }
    public String toLanguageTags() {
        StringBuilder result = new StringBuilder();
        for (Locale locale : locales) {
            if (result.length() > 0) result.append(',');
            result.append(locale.toLanguageTag());
        }
        return result.toString();
    }
    public static LocaleList getDefault() { return new LocaleList(Locale.getDefault()); }
    public static LocaleList getAdjustedDefault() { return getDefault(); }
    public static LocaleList getEmptyLocaleList() { return EMPTY; }
    @Override public boolean equals(Object other) {
        return other instanceof LocaleList &&
            Arrays.equals(locales, ((LocaleList)other).locales);
    }
    @Override public int hashCode() { return Arrays.hashCode(locales); }
}
