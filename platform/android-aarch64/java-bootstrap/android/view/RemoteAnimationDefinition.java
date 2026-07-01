package android.view;

import java.util.HashMap;
import java.util.Map;

public class RemoteAnimationDefinition {
    private final Map<Integer, RemoteAnimationAdapter> adapters =
        new HashMap<Integer, RemoteAnimationAdapter>();
    public void addRemoteAnimation(int transition, RemoteAnimationAdapter adapter) {
        adapters.put(transition, adapter);
    }
    public void addRemoteAnimation(int transition, int activityTypeFilter,
            RemoteAnimationAdapter adapter) {
        adapters.put(transition, adapter);
    }
}
