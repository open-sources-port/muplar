package android.net;

public class DhcpInfo {
    public int ipAddress;
    public int gateway;
    public int netmask;
    public int dns1;
    public int dns2;
    public int serverAddress;
    public int leaseDuration;

    public DhcpInfo() {
    }

    @Override
    public String toString() {
        return "ipaddr " + ipAddress + " gateway " + gateway + " netmask " + netmask
                + " dns1 " + dns1 + " dns2 " + dns2 + " DHCP server " + serverAddress
                + " lease " + leaseDuration + " seconds";
    }
}
