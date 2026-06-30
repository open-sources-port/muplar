package android.content.pm;

import java.util.Collections;
import java.util.List;

public class ShortcutManager {
    public List<ShortcutInfo> getDynamicShortcuts() { return Collections.emptyList(); }
    public List<ShortcutInfo> getPinnedShortcuts() { return Collections.emptyList(); }
    public boolean isRequestPinShortcutSupported() { return false; }
    public boolean requestPinShortcut(ShortcutInfo shortcut, Object callback) {
        return false;
    }
}
