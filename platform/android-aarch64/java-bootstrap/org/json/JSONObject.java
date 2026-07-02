package org.json;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class JSONObject {
    private static final Pattern INTEGER = Pattern.compile(
        "\\\"([^\\\"]+)\\\"\\s*:\\s*(-?\\d+)");
    private static final Pattern ARRAY = Pattern.compile(
        "\\\"([^\\\"]+)\\\"\\s*:\\s*\\[(.*?)\\]", Pattern.DOTALL);
    private static final Pattern STRING = Pattern.compile(
        "\\\"((?:\\\\.|[^\\\"\\\\])*)\\\"");
    private final Map<String, Integer> integers = new HashMap<String, Integer>();
    private final Map<String, JSONArray> arrays = new HashMap<String, JSONArray>();

    public JSONObject(String source) throws JSONException {
        if (source == null) throw new JSONException("null JSON");
        String clean = source.replaceAll("(?m)^\\s*//.*$", "");
        Matcher integerMatcher = INTEGER.matcher(clean);
        while (integerMatcher.find()) {
            integers.put(integerMatcher.group(1),
                Integer.valueOf(integerMatcher.group(2)));
        }
        Matcher arrayMatcher = ARRAY.matcher(clean);
        while (arrayMatcher.find()) {
            List<String> entries = new ArrayList<String>();
            Matcher stringMatcher = STRING.matcher(arrayMatcher.group(2));
            while (stringMatcher.find()) entries.add(unescape(stringMatcher.group(1)));
            arrays.put(arrayMatcher.group(1), new JSONArray(entries));
        }
    }

    public int getInt(String name) throws JSONException {
        Integer value = integers.get(name);
        if (value == null) throw new JSONException("missing integer: " + name);
        return value.intValue();
    }
    public boolean has(String name) {
        return integers.containsKey(name) || arrays.containsKey(name);
    }
    public JSONArray getJSONArray(String name) throws JSONException {
        JSONArray value = arrays.get(name);
        if (value == null) throw new JSONException("missing array: " + name);
        return value;
    }

    private static String unescape(String value) {
        return value.replace("\\\"", "\"").replace("\\\\", "\\");
    }
}
