package com.muplar.runtime;

import android.content.res.Resources;
import android.content.res.TypedArray;
import android.content.res.XmlResourceParser;
import android.os.Bundle;
import android.util.TypedValue;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.Map;
import org.xmlpull.v1.XmlPullParser;

/** Application metadata and component themes from the APK's compiled manifest. */
final class MuplarManifestInfo {
    int applicationTheme;
    final Bundle metadata = new Bundle();
    final Map<String, Integer> themes = new HashMap<>();
    final Map<String, String> aliases = new HashMap<>();
    final Map<String, Boolean> libraries = new LinkedHashMap<>();

    static MuplarManifestInfo read(Resources resources, String packageName) {
        MuplarManifestInfo result = new MuplarManifestInfo();
        try (XmlResourceParser parser = resources.getAssets()
                .openXmlResourceParser("AndroidManifest.xml")) {
            int appDepth = -1;
            for (int event = parser.next(); event != XmlPullParser.END_DOCUMENT;
                    event = parser.next()) {
                if (event == XmlPullParser.END_TAG && parser.getDepth() == appDepth) {
                    appDepth = -1;
                }
                if (event != XmlPullParser.START_TAG) continue;
                String tag = parser.getName();
                if ("application".equals(tag)) {
                    appDepth = parser.getDepth();
                    TypedArray attrs = resources.obtainAttributes(parser,
                        new int[] {android.R.attr.theme});
                    try {
                        result.applicationTheme = attrs.getResourceId(0, 0);
                    } finally {
                        attrs.recycle();
                    }
                } else if (appDepth >= 0 && parser.getDepth() == appDepth + 1) {
                    if ("meta-data".equals(tag)) {
                        result.readMetadata(resources, parser);
                    } else if ("uses-library".equals(tag)) {
                        String ns = "http://schemas.android.com/apk/res/android";
                        String name = parser.getAttributeValue(ns, "name");
                        if (name != null) {
                            boolean required = parser.getAttributeBooleanValue(ns, "required", true);
                            result.libraries.put(name, required || Boolean.TRUE.equals(result.libraries.get(name)));
                        }
                    } else if ("activity".equals(tag) || "activity-alias".equals(tag)) {
                        String ns = "http://schemas.android.com/apk/res/android";
                        String name = normalize(packageName, parser.getAttributeValue(ns, "name"));
                        int theme = parser.getAttributeResourceValue(ns, "theme", 0);
                        if (name != null && theme != 0) result.themes.put(name, theme);
                        if ("activity-alias".equals(tag) && name != null) {
                            result.aliases.put(name, normalize(packageName,
                                parser.getAttributeValue(ns, "targetActivity")));
                        }
                    }
                }
            }
            return result;
        } catch (Exception e) {
            System.err.println("[Muplar/ART] manifest metadata unavailable: " + e);
            // Do not expose partially parsed data after an error.
            return new MuplarManifestInfo();
        }
    }

    int themeFor(String packageName, String activity) {
        String name = normalize(packageName, activity);
        Integer theme = themes.get(name);
        if (theme == null) theme = themes.get(aliases.get(name));
        return theme == null ? applicationTheme : theme;
    }

    private void readMetadata(Resources resources, XmlResourceParser parser) {
        TypedArray attrs = resources.obtainAttributes(parser,
            new int[] {android.R.attr.name, android.R.attr.value, android.R.attr.resource});
        try {
            String name = attrs.getString(0);
            if (name == null) return;
            int resource = attrs.getResourceId(2, 0);
            if (resource != 0) {
                metadata.putInt(name, resource);
                return;
            }
            TypedValue value = attrs.peekValue(1);
            if (value == null) return;
            if (value.type == TypedValue.TYPE_STRING) {
                metadata.putString(name, attrs.getString(1));
            } else if (value.type == TypedValue.TYPE_INT_BOOLEAN) {
                metadata.putBoolean(name, value.data != 0);
            } else if (value.type >= TypedValue.TYPE_FIRST_INT &&
                       value.type <= TypedValue.TYPE_LAST_INT) {
                metadata.putInt(name, value.data);
            } else if (value.type == TypedValue.TYPE_FLOAT) {
                metadata.putFloat(name, value.getFloat());
            }
        } finally {
            attrs.recycle();
        }
    }

    private static String normalize(String pkg, String name) {
        if (name == null || name.isEmpty()) return null;
        if (name.startsWith(".")) return pkg + name;
        return name.indexOf('.') < 0 ? pkg + "." + name : name;
    }
}
