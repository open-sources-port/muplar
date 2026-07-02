package android.app.admin;

public class DevicePolicyManager {
    private final DevicePolicyResourcesManager resources =
        new DevicePolicyResourcesManager();
    public DevicePolicyResourcesManager getResources() { return resources; }
    public ManagedSubscriptionsPolicy getManagedSubscriptionsPolicy() {
        return new ManagedSubscriptionsPolicy(0);
    }
}
