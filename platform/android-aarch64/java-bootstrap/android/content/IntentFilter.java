package android.content;

import java.util.LinkedHashSet;
import java.util.Set;

public class IntentFilter {
    private final Set<String> actions = new LinkedHashSet<>();
    private final Set<String> dataSchemes = new LinkedHashSet<>();
    public IntentFilter() {}
    public IntentFilter(String action) { addAction(action); }
    public void addAction(String action) { if (action != null) actions.add(action); }
    public boolean hasAction(String action) { return actions.contains(action); }
    public int countActions() { return actions.size(); }
    public void addDataScheme(String scheme) {
        if (scheme != null) dataSchemes.add(scheme);
    }
    public boolean hasDataScheme(String scheme) { return dataSchemes.contains(scheme); }
    public void addDataSchemeSpecificPart(String part, int type) {}
}
