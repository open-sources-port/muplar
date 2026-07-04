package android.app.admin;

public class ManagedSubscriptionsPolicy {
    private final int policyType;
    public ManagedSubscriptionsPolicy(int policyType) { this.policyType = policyType; }
    public int getPolicyType() { return policyType; }
}
