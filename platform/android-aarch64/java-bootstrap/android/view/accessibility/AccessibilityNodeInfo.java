package android.view.accessibility;

public class AccessibilityNodeInfo {
    public static final class AccessibilityAction {
        private final int id;
        private final CharSequence label;
        public AccessibilityAction(int id, CharSequence label) {
            this.id = id;
            this.label = label;
        }
        public int getId() { return id; }
        public CharSequence getLabel() { return label; }
    }
}
