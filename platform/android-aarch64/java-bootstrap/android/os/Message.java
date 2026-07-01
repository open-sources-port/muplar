package android.os;

public final class Message {
    public int what;
    public int arg1;
    public int arg2;
    public Object obj;
    public Handler target;
    Runnable callback;

    public static Message obtain() { return new Message(); }
    public static Message obtain(Handler target) {
        Message message = new Message(); message.target = target; return message;
    }
    public static Message obtain(Handler target, int what) {
        Message message = obtain(target); message.what = what; return message;
    }
    public static Message obtain(Handler target, int what, Object object) {
        Message message = obtain(target, what); message.obj = object; return message;
    }
    public static Message obtain(Handler target, int what, int arg1, int arg2) {
        Message message = obtain(target, what); message.arg1 = arg1;
        message.arg2 = arg2; return message;
    }
    public static Message obtain(Handler target, int what, int arg1, int arg2,
                                 Object object) {
        Message message = obtain(target, what, arg1, arg2);
        message.obj = object; return message;
    }
    public static Message obtain(Handler target, Runnable callback) {
        Message message = obtain(target); message.callback = callback; return message;
    }
    public void sendToTarget() {
        if (target != null) target.sendMessage(this);
    }
}
