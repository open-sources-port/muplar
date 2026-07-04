package android.util;

public final class StateSet {
    public static final int[] WILD_CARD = new int[0];
    public static final int[] NOTHING = new int[]{0};
    private StateSet() {}
    public static boolean stateSetMatches(int[] stateSpec, int[] stateSet) {
        if (stateSpec == null || stateSpec.length == 0) return true;
        for (int state : stateSpec) {
            if (state == 0) return true;
            boolean required = state > 0;
            int wanted = required ? state : -state;
            boolean present = false;
            if (stateSet != null)
                for (int current : stateSet) if (current == wanted) present = true;
            if (required != present) return false;
        }
        return true;
    }
    public static boolean isWildCard(int[] stateSet) {
        return stateSet == null || stateSet.length == 0 || stateSet[0] == 0;
    }
    public static int[] trimStateSet(int[] states, int newSize) {
        if (states.length == newSize) return states;
        int[] trimmed = new int[newSize];
        System.arraycopy(states, 0, trimmed, 0, newSize);
        return trimmed;
    }
}
