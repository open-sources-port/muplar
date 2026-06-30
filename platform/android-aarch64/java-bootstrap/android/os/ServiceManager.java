package android.os;

import com.muplar.runtime.FrameworkServiceClient;
import java.util.Base64;

public final class ServiceManager {
    private ServiceManager() {}

    public static IBinder checkService(String name) {
        String state = FrameworkServiceClient.request("check-service", name);
        return "2".equals(state) ? new RemoteBinder(name) : null;
    }
    public static IBinder getService(String name) { return checkService(name); }
    public static IBinder waitForService(String name) { return checkService(name); }
    public static boolean isDeclared(String name) {
        String state = FrameworkServiceClient.request("check-service", name);
        return "1".equals(state) || "2".equals(state);
    }
    public static void addService(String name, IBinder service) {
        throw new UnsupportedOperationException(
            "Java service ownership requires a persistent system-server host");
    }

    private static final class RemoteBinder implements IBinder {
        private final String serviceName;
        RemoteBinder(String serviceName) { this.serviceName = serviceName; }
        public String getInterfaceDescriptor() { return serviceName; }
        public boolean pingBinder() { return isBinderAlive(); }
        public boolean isBinderAlive() {
            return "2".equals(FrameworkServiceClient.request(
                "check-service", serviceName));
        }
        public IInterface queryLocalInterface(String descriptor) { return null; }
        public boolean transact(int code, Parcel data, Parcel reply, int flags)
            throws RemoteException {
            Parcel request = data == null ? Parcel.obtain() : data;
            String encoded = Base64.getEncoder().encodeToString(
                request.encode(code, flags));
            String response = FrameworkServiceClient.request("binder-transact",
                serviceName + "\n" + encoded);
            if (response == null || "DEAD_OBJECT".equals(response))
                throw new RemoteException("Binder service died: " + serviceName);
            if ((flags & FLAG_ONEWAY) == 0 && reply != null) {
                try {
                    reply.decode(Base64.getDecoder().decode(response));
                } catch (IllegalArgumentException error) {
                    throw new RemoteException("Invalid Binder response");
                }
            }
            return true;
        }
    }
}
