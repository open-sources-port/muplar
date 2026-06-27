package android.content;

public class Intent {
    private String pkg;
    private String cls;

    public Intent() {}

    public Intent setClassName(String pkg, String cls) {
        this.pkg = pkg;
        this.cls = cls;
        return this;
    }

    public String getComponentPackage() {
        return pkg;
    }

    public String getComponentClass() {
        return cls;
    }
}
