package android.os;

import java.util.concurrent.CopyOnWriteArrayList;

public final class MessageQueue {
    public interface IdleHandler { boolean queueIdle(); }
    private final CopyOnWriteArrayList<IdleHandler> idleHandlers =
        new CopyOnWriteArrayList<IdleHandler>();

    public boolean isIdle() { return true; }
    public void addIdleHandler(IdleHandler handler) {
        if (handler == null) return;
        idleHandlers.addIfAbsent(handler);
        if (!handler.queueIdle()) idleHandlers.remove(handler);
    }
    public void removeIdleHandler(IdleHandler handler) { idleHandlers.remove(handler); }
}
