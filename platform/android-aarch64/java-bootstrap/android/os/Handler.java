package android.os;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;

public class Handler {
    public interface Callback { boolean handleMessage(Message message); }
    private final Looper looper;
    private final Callback callback;
    private final Map<Object, List<ScheduledFuture<?>>> pending =
        new ConcurrentHashMap<Object, List<ScheduledFuture<?>>>();

    public Handler() { this(Looper.getMainLooper(), null); }
    public Handler(Callback callback) { this(Looper.getMainLooper(), callback); }
    public Handler(Looper looper) { this(looper, null); }
    public Handler(Looper looper, Callback callback) {
        this.looper = looper == null ? Looper.getMainLooper() : looper;
        this.callback = callback;
    }
    public Looper getLooper() { return looper; }
    public void dispatchMessage(Message message) {
        if (message.callback != null) message.callback.run();
        else if (callback == null || !callback.handleMessage(message))
            handleMessage(message);
    }
    public void handleMessage(Message message) {}
    public boolean sendMessage(Message message) {
        return sendMessageDelayed(message, 0);
    }
    public boolean sendEmptyMessage(int what) {
        return sendMessage(Message.obtain(this, what));
    }
    public boolean sendEmptyMessageDelayed(int what, long delayMillis) {
        return sendMessageDelayed(Message.obtain(this, what), delayMillis);
    }
    public boolean sendMessageDelayed(final Message message, long delayMillis) {
        message.target = this;
        schedule(message, new Runnable() {
            @Override public void run() { dispatchMessage(message); }
        }, delayMillis);
        return true;
    }
    public boolean sendMessageAtFrontOfQueue(Message message) {
        return sendMessage(message);
    }
    public boolean post(Runnable action) { return postDelayed(action, 0); }
    public boolean postDelayed(final Runnable action, long delayMillis) {
        schedule(action, action, delayMillis); return true;
    }
    public boolean postAtFrontOfQueue(Runnable action) { return post(action); }
    public void removeMessages(int what) {
        cancel(Integer.valueOf(what));
    }
    public void removeCallbacks(Runnable action) { cancel(action); }
    public void removeCallbacksAndMessages(Object token) {
        if (token != null) { cancel(token); return; }
        for (Object key : new ArrayList<Object>(pending.keySet())) cancel(key);
    }
    public boolean hasMessages(int what) {
        List<ScheduledFuture<?>> futures = pending.get(Integer.valueOf(what));
        return futures != null && !futures.isEmpty();
    }
    private void schedule(Object key, Runnable action, long delayMillis) {
        ScheduledFuture<?> future = looper.executor.schedule(action,
            Math.max(0, delayMillis), TimeUnit.MILLISECONDS);
        List<ScheduledFuture<?>> futures = pending.get(key);
        if (futures == null) {
            futures = Collections.synchronizedList(
                new ArrayList<ScheduledFuture<?>>());
            pending.put(key, futures);
        }
        futures.add(future);
    }
    private void schedule(Message message, Runnable action, long delayMillis) {
        schedule(Integer.valueOf(message.what), action, delayMillis);
    }
    private void cancel(Object key) {
        List<ScheduledFuture<?>> futures = pending.remove(key);
        if (futures == null) return;
        for (ScheduledFuture<?> future : futures) future.cancel(false);
    }
}
