package org.json;

import java.util.ArrayList;
import java.util.List;

public class JSONArray {
    private final List<String> values;

    JSONArray(List<String> values) {
        this.values = new ArrayList<String>(values);
    }

    public int length() { return values.size(); }
    public String getString(int index) throws JSONException {
        if (index < 0 || index >= values.size())
            throw new JSONException("index out of range: " + index);
        return values.get(index);
    }
}
