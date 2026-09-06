package com.muplar.runtime;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.Map;

public final class FrameworkDeviceController {
    // Opcode values must match muplar::services::Opcode in
    // services/muplard_protocol.h.
    private static final int OPCODE_SUBSCRIBE_DEVICE_ACTIONS = 25;
    private static final int OPCODE_DEVICE_ACTION_CHANGED = 26;
    private static final int OPCODE_SUBSCRIBE_DEVICE_INPUTS = 28;
    private static final int OPCODE_DEVICE_INPUT_CHANGED = 29;

    private static final class DeviceAction {
        final long generation;
        final String action;
        final String tab;
        final String apkPath;
        final String packageName;
        final String activityName;
        final String applicationName;

        DeviceAction(long generation,
                     String action,
                     String tab,
                     String apkPath,
                     String packageName,
                     String activityName,
                     String applicationName) {
            this.generation = generation;
            this.action = clean(action);
            this.tab = clean(tab);
            this.apkPath = clean(apkPath);
            this.packageName = clean(packageName);
            this.activityName = clean(activityName);
            this.applicationName = clean(applicationName);
        }
    }

    private static final class DeviceInput {
        final long generation;
        final String tab;
        final int type;
        final int action;
        final int source;
        final int deviceId;
        final int keyCode;
        final float x;
        final float y;

        DeviceInput(long generation,
                    String tab,
                    int type,
                    int action,
                    int source,
                    int deviceId,
                    int keyCode,
                    float x,
                    float y) {
            this.generation = generation;
            this.tab = clean(tab);
            this.type = type;
            this.action = action;
            this.source = source;
            this.deviceId = deviceId;
            this.keyCode = keyCode;
            this.x = x;
            this.y = y;
        }
    }

    public static final class TaskRecord {
        public final int taskId;
        public final String affinity;
        public final String tab;
        public final ArrayList<ActivityRecord> activityStack =
            new ArrayList<ActivityRecord>();

        TaskRecord(int taskId, String affinity, String tab) {
            this.taskId = taskId;
            this.affinity = affinity;
            this.tab = tab;
        }

        public synchronized ActivityRecord topActivity() {
            if (activityStack.isEmpty()) return null;
            return activityStack.get(activityStack.size() - 1);
        }

        public synchronized ActivityRecord rootActivity() {
            if (activityStack.isEmpty()) return null;
            return activityStack.get(0);
        }

        public synchronized void pushActivity(ActivityRecord record) {
            activityStack.remove(record);
            activityStack.add(record);
            record.task = this;
        }

        public synchronized boolean removeActivity(ActivityRecord record) {
            boolean removed = activityStack.remove(record);
            if (removed) record.task = null;
            return removed;
        }

        public synchronized int size() {
            return activityStack.size();
        }

        public synchronized boolean isEmpty() {
            return activityStack.isEmpty();
        }

        @Override public String toString() {
            return "TaskRecord{id=" + taskId + ", tab=" + tab + ", size=" + activityStack.size() + "}";
        }
    }

    public static final class ActivityRecord {
        public TaskRecord task;
        public android.os.IBinder token;
        public String tab;
        public String apkPath;
        public String packageName;
        public String activityName;
        public String applicationName;
        public Object activity;
        public Class<?> activityClass;
        public boolean foreground;
        public boolean finishing;
    }

    private static final Object lock = new Object();
    private static int nextTaskId = 1;
    private static final Map<Integer, TaskRecord> tasks =
        new LinkedHashMap<Integer, TaskRecord>();
    private static final Map<String, TaskRecord> tasksByTab =
        new LinkedHashMap<String, TaskRecord>();
    private static final ArrayList<TaskRecord> taskStack =
        new ArrayList<TaskRecord>();
    private static final Map<android.os.IBinder, ActivityRecord> recordsByToken =
        new LinkedHashMap<android.os.IBinder, ActivityRecord>();
    private static final Map<String, ActivityRecord> activities =
        new LinkedHashMap<String, ActivityRecord>();
    private static final ArrayList<String> activityStack =
        new ArrayList<String>();
    private static final ArrayList<DeviceAction> pendingActions =
        new ArrayList<DeviceAction>();
    private static final ArrayList<DeviceInput> pendingInputs =
        new ArrayList<DeviceInput>();

    private static volatile MuplarSocketClient subscriber;
    private static volatile MuplarSocketClient inputSubscriber;
    private static volatile long generation;
    private static volatile long lastFocusGeneration;
    private static volatile long inputGeneration;
    private static volatile String activeTab = "launcher";
    private static volatile boolean mainReady;
    private static volatile long pointerDownTime;
    private static volatile float pointerDownX;
    private static volatile float pointerDownY;

    private FrameworkDeviceController() {}

    public static void registerActivity(Object value,
                                        String packageValue,
                                        String activityValue,
                                        Class<?> type) {
        registerActivity(value, packageValue, activityValue, type, null);
    }

    public static void registerActivity(Object value,
                                        String packageValue,
                                        String activityValue,
                                        Class<?> type,
                                        android.os.IBinder token) {
        registerActivityForTab(defaultTabFor(packageValue, activityValue),
            "", packageValue, activityValue, value, type, token);
    }

    public static void registerActivityForTab(String tabValue,
                                             String apkValue,
                                             String packageValue,
                                             String activityValue,
                                             Object value,
                                             Class<?> type) {
        registerActivityForTab(tabValue, apkValue, packageValue, activityValue, value, type, null);
    }

    public static void registerActivityForTab(String tabValue,
                                             String apkValue,
                                             String packageValue,
                                             String activityValue,
                                             Object value,
                                             Class<?> type,
                                             android.os.IBinder token) {
        String normalizedPackage = clean(packageValue);
        String normalizedActivity =
            normalizeActivityName(normalizedPackage, activityValue);
        String normalizedTab = clean(tabValue);
        if (normalizedTab.isEmpty())
            normalizedTab = defaultTabFor(normalizedPackage, normalizedActivity);
        ActivityRecord record = new ActivityRecord();
        record.tab = normalizedTab;
        record.apkPath = clean(apkValue);
        record.packageName = normalizedPackage;
        record.activityName = normalizedActivity;
        record.activity = value;
        record.activityClass =
            type == null && value != null ? value.getClass() : type;
        record.foreground = value != null;
        record.token = token;

        synchronized (lock) {
            TaskRecord task = tasksByTab.get(normalizedTab);
            if (task == null) {
                task = new TaskRecord(nextTaskId++, normalizedPackage, normalizedTab);
                tasks.put(task.taskId, task);
                tasksByTab.put(normalizedTab, task);
            }
            task.pushActivity(record);
            if (token != null) {
                recordsByToken.put(token, record);
            }
            activities.put(normalizedTab, record);
            activeTab = normalizedTab;
            mainReady = true;
            taskStack.remove(task);
            taskStack.add(task);
            activityStack.remove(normalizedTab);
            activityStack.add(normalizedTab);
        }
        System.out.println("[DeviceController] registered activity tab=" +
            normalizedTab + " package=" + normalizedPackage + " activity=" +
            normalizedActivity + " task=" + record.task + " token=" + (token != null));
        flushPendingActions();
        start();
    }

    public static ActivityRecord findActivityByToken(android.os.IBinder token) {
        if (token == null) return null;
        synchronized (lock) {
            return recordsByToken.get(token);
        }
    }

    public static void launchApp(String apkPath,
                                 String packageName,
                                 String activityName,
                                 String applicationName) {
        if (packageName == null || packageName.isEmpty()) {
            return;
        }
        long nextGen;
        synchronized (lock) {
            nextGen = ++generation;
            lastFocusGeneration = nextGen;
        }
        String tab = packageName;
        DeviceAction action = new DeviceAction(
            nextGen, "focus-tab", tab, apkPath, packageName, activityName, applicationName);
        System.out.println("[DeviceController] launchApp tab=" + tab
            + " package=" + packageName + " activity=" + activityName);
        dispatchAction(action);
    }

    private static volatile boolean connecting;

    public static synchronized void start() {
        if (subscriber != null || connecting) return;
        connecting = true;
        Thread connectThread = new Thread(new Runnable() {
            @Override public void run() {
                try {
                    for (int i = 0; i < 150; i++) {
                        String socket = FrameworkServiceClient.getServiceSocket();
                        if (!socket.isEmpty()) {
                            if (initSubscriber(socket)) {
                                return;
                            }
                        }
                        try {
                            Thread.sleep(100);
                        } catch (InterruptedException ignored) {
                            break;
                        }
                    }
                    System.err.println("[DeviceController] could not connect to muplard socket after retry");
                } finally {
                    connecting = false;
                }
            }
        }, "muplar-device-connect");
        connectThread.setDaemon(true);
        connectThread.start();
    }

    private static boolean initSubscriber(String socket) {
        synchronized (FrameworkDeviceController.class) {
            if (subscriber != null) return true;
        }
        MuplarSocketClient actionClient = MuplarSocketClient.connect(socket);
        MuplarSocketClient inputClient =
            actionClient == null ? null : MuplarSocketClient.connect(socket);
        if (actionClient == null || inputClient == null) {
            if (actionClient != null) actionClient.close();
            return false;
        }
        MuplarSocketClient.Frame actionAck;
        MuplarSocketClient.Frame inputAck;
        if (!actionClient.sendFrame(OPCODE_SUBSCRIBE_DEVICE_ACTIONS, "") ||
            !inputClient.sendFrame(OPCODE_SUBSCRIBE_DEVICE_INPUTS, "") ||
            (actionAck = actionClient.readFrame()) == null ||
            actionAck.opcode !=
                (OPCODE_SUBSCRIBE_DEVICE_ACTIONS | MuplarSocketClient.REPLY_FLAG) ||
            (inputAck = inputClient.readFrame()) == null ||
            inputAck.opcode !=
                (OPCODE_SUBSCRIBE_DEVICE_INPUTS | MuplarSocketClient.REPLY_FLAG)) {
            actionClient.close();
            inputClient.close();
            return false;
        }
        synchronized (FrameworkDeviceController.class) {
            subscriber = actionClient;
            inputSubscriber = inputClient;
        }
        Thread reader = new Thread(new Runnable() {
            @Override public void run() {
                readActions(actionClient);
            }
        }, "muplar-device-actions");
        reader.setDaemon(true);
        reader.start();
        Thread inputReader = new Thread(new Runnable() {
            @Override public void run() {
                readInputs(inputClient);
            }
        }, "muplar-device-inputs");
        inputReader.setDaemon(true);
        inputReader.start();
        Runtime.getRuntime().addShutdownHook(new Thread(new Runnable() {
            @Override public void run() {
                MuplarSocketClient client = subscriber;
                if (client != null) client.close();
                MuplarSocketClient input = inputSubscriber;
                if (input != null) input.close();
            }
        }, "muplar-device-actions-shutdown"));
        System.out.println("[DeviceController] subscribed");
        return true;
    }

    private static void readActions(MuplarSocketClient client) {
        try {
            MuplarSocketClient.Frame frame;
            while ((frame = client.readFrame()) != null) {
                if (frame.opcode != OPCODE_DEVICE_ACTION_CHANGED)
                    continue;
                applyAction(parseAction(frame.payload));
            }
        } catch (Exception error) {
            System.err.println("[DeviceController] stream failed: " + error);
        } finally {
            client.close();
            subscriber = null;
            // Auto-reconnect: muplard may have restarted; retry connection.
            System.out.println("[DeviceController] action stream ended, reconnecting...");
            start();
        }
    }

    private static DeviceAction parseAction(String payload) {
        long nextGeneration = generation;
        String nextAction = "";
        String nextTab = "";
        String nextApk = "";
        String nextPackage = "";
        String nextActivity = "";
        String nextApplication = "";
        for (String line : payload.split("\n", -1)) {
            if (line.startsWith("generation=")) {
                nextGeneration = parseLong(line.substring(11), generation);
            } else if (line.startsWith("action=")) {
                nextAction = line.substring(7);
            } else if (line.startsWith("tab=")) {
                nextTab = line.substring(4);
            } else if (line.startsWith("apk=")) {
                nextApk = line.substring(4);
            } else if (line.startsWith("package=")) {
                nextPackage = line.substring(8);
            } else if (line.startsWith("activity=")) {
                nextActivity = line.substring(9);
            } else if (line.startsWith("application=")) {
                nextApplication = line.substring(12);
            }
        }
        return new DeviceAction(nextGeneration, nextAction, nextTab, nextApk,
            nextPackage, nextActivity, nextApplication);
    }

    private static void readInputs(MuplarSocketClient client) {
        try {
            MuplarSocketClient.Frame frame;
            while ((frame = client.readFrame()) != null) {
                if (frame.opcode != OPCODE_DEVICE_INPUT_CHANGED)
                    continue;
                DeviceInput parsed = parseInput(frame.payload);
                System.out.println("[DeviceController] input frame gen=" +
                    parsed.generation + " tab=" + parsed.tab + " type=" +
                    parsed.type + " action=" + parsed.action +
                    " localInputGeneration=" + inputGeneration);
                System.out.flush();
                applyInput(parsed);
            }
        } catch (Exception error) {
            System.err.println("[DeviceController] input stream failed: " +
                error);
        } finally {
            client.close();
            inputSubscriber = null;
            // Auto-reconnect handled by the action stream's finally block.
        }
    }

    private static DeviceInput parseInput(String payload) {
        long nextGeneration = inputGeneration;
        String nextTab = "";
        int nextType = 0;
        int nextAction = 0;
        int nextSource = 0;
        int nextDeviceId = 0;
        int nextKeyCode = 0;
        float nextX = 0.0f;
        float nextY = 0.0f;
        for (String line : payload.split("\n", -1)) {
            if (line.startsWith("generation=")) {
                nextGeneration = parseLong(line.substring(11), inputGeneration);
            } else if (line.startsWith("tab=")) {
                nextTab = line.substring(4);
            } else if (line.startsWith("type=")) {
                nextType = parseInt(line.substring(5), 0);
            } else if (line.startsWith("action=")) {
                nextAction = parseInt(line.substring(7), 0);
            } else if (line.startsWith("source=")) {
                nextSource = parseInt(line.substring(7), 0);
            } else if (line.startsWith("deviceId=")) {
                nextDeviceId = parseInt(line.substring(9), 0);
            } else if (line.startsWith("keyCode=")) {
                nextKeyCode = parseInt(line.substring(8), 0);
            } else if (line.startsWith("x=")) {
                nextX = parseFloat(line.substring(2), 0.0f);
            } else if (line.startsWith("y=")) {
                nextY = parseFloat(line.substring(2), 0.0f);
            }
        }
        return new DeviceInput(nextGeneration, nextTab, nextType, nextAction,
            nextSource, nextDeviceId, nextKeyCode, nextX, nextY);
    }

    private static long parseLong(String value, long fallback) {
        try {
            return Long.parseLong(value);
        } catch (NumberFormatException error) {
            return fallback;
        }
    }

    private static int parseInt(String value, int fallback) {
        try {
            return Integer.parseInt(value);
        } catch (NumberFormatException error) {
            return fallback;
        }
    }

    private static float parseFloat(String value, float fallback) {
        try {
            return Float.parseFloat(value);
        } catch (NumberFormatException error) {
            return fallback;
        }
    }

    private static void applyAction(DeviceAction next) {
        synchronized (lock) {
            if (next.generation <= generation) return;
            generation = next.generation;
            if ("focus-tab".equals(next.action)) {
                lastFocusGeneration = next.generation;
            }
        }
        System.out.println("[DeviceController] action=" + next.action +
            " tab=" + next.tab + " generation=" + next.generation);
        dispatchAction(next);
    }

    private static void applyInput(DeviceInput next) {
        synchronized (lock) {
            if (next.generation <= inputGeneration) return;
            inputGeneration = next.generation;
        }
        dispatchInput(next);
    }

    private static void dispatchAction(final DeviceAction next) {
        try {
            android.os.Looper looper = android.os.Looper.getMainLooper();
            System.out.println("[DeviceController] dispatchAction action=" + next.action +
                " tab=" + next.tab + " looper=" + (looper != null));
            if (looper == null) {
                System.out.println("[DeviceController] dispatchAction: queuing because looper is null");
                queueAction(next);
                return;
            }
            boolean posted = android.os.Handler.createAsync(looper).post(new Runnable() {
                @Override public void run() {
                    boolean ready = isReadyFor(next);
                    System.out.println("[DeviceController] dispatchAction run on main: ready=" + ready +
                        " action=" + next.action + " tab=" + next.tab);
                    if (!ready) {
                        queueAction(next);
                        return;
                    }
                    applyActionOnMain(next);
                    flushPendingActions();
                }
            });
            System.out.println("[DeviceController] dispatchAction posted=" + posted +
                " action=" + next.action);
        } catch (Throwable error) {
            System.err.println("[DeviceController] dispatch failed: " +
                error.getClass().getName() + ": " + error.getMessage());
        }
    }

    private static void dispatchInput(final DeviceInput next) {
        try {
            android.os.Looper looper = android.os.Looper.getMainLooper();
            if (looper == null) {
                queueInput(next);
                return;
            }
            android.os.Handler.createAsync(looper).post(new Runnable() {
                @Override public void run() {
                    if (!mainReady) {
                        queueInput(next);
                        return;
                    }
                    applyInputOnMain(next);
                    flushPendingInputs();
                }
            });
        } catch (Throwable error) {
            System.err.println("[DeviceController] input dispatch failed: " +
                error.getClass().getName() + ": " + error.getMessage());
        }
    }

    private static boolean isReadyFor(DeviceAction next) {
        if ("focus-tab".equals(next.action) && !isHostTab(next.tab) &&
            !hasActivityForTab(next.tab)) {
            return true;
        }
        return mainReady;
    }

    private static void queueAction(DeviceAction next) {
        synchronized (lock) {
            pendingActions.add(next);
        }
    }

    private static void queueInput(DeviceInput next) {
        synchronized (lock) {
            pendingInputs.add(next);
        }
    }

    private static void flushPendingActions() {
        android.os.Looper looper = android.os.Looper.getMainLooper();
        if (looper == null) return;
        android.os.Handler.createAsync(looper).post(new Runnable() {
            @Override public void run() {
                ArrayList<DeviceAction> actions;
                synchronized (lock) {
                    if (!mainReady || pendingActions.isEmpty()) return;
                    actions = new ArrayList<DeviceAction>(pendingActions);
                    pendingActions.clear();
                }
                for (DeviceAction action : actions) {
                    applyActionOnMain(action);
                }
                flushPendingInputs();
            }
        });
    }

    private static void flushPendingInputs() {
        android.os.Looper looper = android.os.Looper.getMainLooper();
        if (looper == null) return;
        android.os.Handler.createAsync(looper).post(new Runnable() {
            @Override public void run() {
                ArrayList<DeviceInput> inputs;
                synchronized (lock) {
                    if (!mainReady || pendingInputs.isEmpty()) return;
                    inputs = new ArrayList<DeviceInput>(pendingInputs);
                    pendingInputs.clear();
                }
                for (DeviceInput input : inputs) {
                    applyInputOnMain(input);
                }
            }
        });
    }

    private static void applyActionOnMain(DeviceAction next) {
        try {
            boolean stale = isStale(next);
            System.out.println("[DeviceController] applyActionOnMain action=" +
                next.action + " tab=" + next.tab + " isStale=" + stale);
            if (stale) return;
            if ("back".equals(next.action)) {
                performBack();
            } else if ("home".equals(next.action)) {
                ActivityRecord launcher = activityForTab("launcher");
                if (launcher != null) {
                    goToNormalState(launcher);
                    focusRecord(launcher);
                } else {
                    moveActiveActivityToBackground();
                }
            } else if ("all-apps".equals(next.action) || "apps".equals(next.action)) {
                toggleAllApps();
            } else if ("recents".equals(next.action)) {
                toggleRecents();
            } else if ("focus-tab".equals(next.action)) {
                if ("launcher".equals(next.tab)) {
                    focusRecord(activityForTab("launcher"));
                } else if (!isHostTab(next.tab)) {
                    focusOrLaunchActivity(next);
                }
            } else if ("close-tab".equals(next.action)) {
                if (!isHostTab(next.tab)) {
                    finishTab(next.tab);
                }
            } else if ("package-installed".equals(next.action)) {
                String pkg = next.packageName;
                if (pkg == null || pkg.isEmpty()) {
                    pkg = next.tab;
                }
                System.out.println("[DeviceController] package installed notification: " + pkg);
                if (pkg != null && !pkg.isEmpty()) {
                    MuplarServices.notifyPackageAdded(pkg);
                }
            } else if ("settings".equals(next.action) ||
                       "install-apk".equals(next.action)) {
                System.out.println("[DeviceController] host action=" +
                    next.action + " tab=" + next.tab);
            }
        } catch (Throwable error) {
            System.err.println("[DeviceController] apply " + next.action +
                " failed: " + error.getClass().getName() + ": " +
                error.getMessage());
        }
    }

    private static void applyInputOnMain(DeviceInput input) {
        try {
            ActivityRecord record = activityForTab(input.tab);
            if (record == null)
                record = activeRecord();
            System.out.println("[DeviceController] input apply tab=" +
                input.tab + " type=" + input.type + " action=" +
                input.action + " x=" + input.x + " y=" + input.y +
                " record=" + (record != null) + " activity=" +
                (record != null && record.activity != null));
            if (record == null || record.activity == null)
                return;
            if (!record.tab.equals(activeTab))
                focusRecord(record);
            if (input.type == 2) {
                dispatchMotionInput(record, input);
            } else if (input.type == 1) {
                dispatchKeyInput(record, input);
            }
            scheduleFrame(record);
        } catch (Throwable error) {
            System.err.println("[DeviceController] input apply failed: " +
                error.getClass().getName() + ": " + error.getMessage());
        }
    }

    private static void dispatchMotionInput(ActivityRecord record,
                                            DeviceInput input)
        throws Exception {
        long now = android.os.SystemClock.uptimeMillis();
        if (input.action == android.view.MotionEvent.ACTION_DOWN ||
            pointerDownTime == 0) {
            pointerDownTime = now;
            pointerDownX = input.x;
            pointerDownY = input.y;
        }
        System.out.println("[DeviceController] motion trace: before obtain");
        System.out.flush();
        android.view.MotionEvent.PointerProperties props =
            new android.view.MotionEvent.PointerProperties();
        props.id = 0;
        props.toolType = android.view.MotionEvent.TOOL_TYPE_FINGER;
        android.view.MotionEvent.PointerCoords coords =
            new android.view.MotionEvent.PointerCoords();
        coords.x = input.x;
        coords.y = input.y;
        coords.pressure = 1.0f;
        coords.size = 1.0f;
        android.view.MotionEvent event = android.view.MotionEvent.obtain(
            pointerDownTime, now, input.action, 1,
            new android.view.MotionEvent.PointerProperties[] { props },
            new android.view.MotionEvent.PointerCoords[] { coords },
            0 /* metaState */, 0 /* buttonState */,
            1.0f /* xPrecision */, 1.0f /* yPrecision */, input.deviceId,
            0 /* edgeFlags */, input.source, 0 /* flags */);
        try {
            java.lang.reflect.Field f = android.view.MotionEvent.class.getDeclaredField("mNativePtr");
            f.setAccessible(true);
            long ptr = f.getLong(event);
            System.out.println("[DeviceController] reflection mNativePtr = 0x" + Long.toHexString(ptr));
        } catch (Throwable t) {
            t.printStackTrace();
        }
        System.out.println("[DeviceController] motion trace: after obtain event=" + event);
        System.out.flush();
        try {
            System.out.println("[DeviceController] motion trace: before invokeInputDispatch target=" + record.activity);
            System.out.flush();
            if (!invokeInputDispatch(record.activity, "dispatchTouchEvent",
                    android.view.MotionEvent.class, event)) {
                System.out.println("[DeviceController] motion trace: trying decor view");
                System.out.flush();
                dispatchToDecorView(record.activity, "dispatchTouchEvent",
                    android.view.MotionEvent.class, event);
            }
            System.out.println("[DeviceController] motion trace: after dispatch");
            System.out.flush();
        } finally {
            event.recycle();
            System.out.println("[DeviceController] motion trace: after recycle");
            System.out.flush();
            if (input.action == android.view.MotionEvent.ACTION_UP ||
                input.action == android.view.MotionEvent.ACTION_CANCEL) {
                if ("launcher".equals(record.tab) && pointerDownTime > 0) {
                    float dy = input.y - pointerDownY;
                    float dx = Math.abs(input.x - pointerDownX);
                    if (dy < -60.0f && Math.abs(dy) > dx) {
                        System.out.println("[DeviceController] launcher upward swipe detected: dy=" + dy);
                        openAllApps(record);
                    } else if (dy > 60.0f && Math.abs(dy) > dx) {
                        System.out.println("[DeviceController] launcher downward swipe detected: dy=" + dy);
                        goToNormalState(record);
                    }
                }
                pointerDownTime = 0;
            }
        }
    }

    private static void dispatchKeyInput(ActivityRecord record,
                                         DeviceInput input)
        throws Exception {
        android.view.KeyEvent event = new android.view.KeyEvent(input.action,
            input.keyCode);
        if (!invokeInputDispatch(record.activity, "dispatchKeyEvent",
                android.view.KeyEvent.class, event)) {
            dispatchToDecorView(record.activity, "dispatchKeyEvent",
                android.view.KeyEvent.class, event);
        }
    }

    private static boolean invokeInputDispatch(Object target,
                                               String methodName,
                                               Class<?> eventClass,
                                               Object event)
        throws Exception {
        try {
            System.out.println("[DeviceController] invokeInputDispatch: target=" + target + " method=" + methodName);
            System.out.flush();
            java.lang.reflect.Method method =
                target.getClass().getMethod(methodName, eventClass);
            method.setAccessible(true);
            System.out.println("[DeviceController] invokeInputDispatch: calling invoke...");
            System.out.flush();
            Object result = method.invoke(target, event);
            System.out.println("[DeviceController] invokeInputDispatch: invoke returned " + result);
            System.out.flush();
            return Boolean.TRUE.equals(result);
        } catch (NoSuchMethodException ignored) {
            return dispatchToDecorView(target, methodName, eventClass, event);
        }
    }

    private static boolean dispatchToDecorView(Object target,
                                               String methodName,
                                               Class<?> eventClass,
                                               Object event)
        throws Exception {
        java.lang.reflect.Method getWindow =
            target.getClass().getMethod("getWindow");
        Object window = getWindow.invoke(target);
        java.lang.reflect.Method getDecorView =
            window.getClass().getMethod("getDecorView");
        Object decor = getDecorView.invoke(window);
        if (decor == null)
            return false;
        java.lang.reflect.Method method =
            decor.getClass().getMethod(methodName, eventClass);
        method.setAccessible(true);
        Object result = method.invoke(decor, event);
        return Boolean.TRUE.equals(result);
    }

    private static boolean isStale(DeviceAction next) {
        if ("focus-tab".equals(next.action)) {
            synchronized (lock) {
                return next.generation < lastFocusGeneration;
            }
        }
        return false;
    }

    private static void focusOrLaunchActivity(DeviceAction next)
        throws Exception {
        System.out.println("[DeviceController] focusOrLaunchActivity tab=" +
            next.tab + " apk=" + next.apkPath + " pkg=" + next.packageName +
            " act=" + next.activityName);
        ActivityRecord record = activityForTab(next.tab);
        if (record != null && (next.activityName.isEmpty() ||
            sameActivity(record.packageName, record.activityName,
                next.packageName, next.activityName))) {
            focusRecord(record);
            return;
        }
        if (next.apkPath.isEmpty() || next.packageName.isEmpty() ||
            next.activityName.isEmpty()) {
            focusRecord(activeRecord());
            return;
        }
        moveActiveActivityToBackground();
        boolean launched = ArtApkMain.launchActivity(next.tab, next.apkPath,
            next.packageName, next.activityName, next.applicationName);
        if (!launched)
            System.err.println("[DeviceController] app launch ignored package="
                + next.packageName + " activity=" + next.activityName);
    }

    public static void startActivityInCurrentTask(final ActivityRecord caller,
                                                  final String apkPath,
                                                  final String packageName,
                                                  final String activityName,
                                                  final String applicationName) {
        android.os.Looper looper = android.os.Looper.getMainLooper();
        if (looper == null) return;
        android.os.Handler.createAsync(looper).post(new Runnable() {
            @Override public void run() {
                try {
                    System.out.println("[DeviceController] startActivityInCurrentTask pkg="
                        + packageName + " cls=" + activityName);
                    ActivityRecord current = caller != null ? caller : activeRecord();
                    if (current != null && current.foreground) {
                        invokeLifecycle(current, "onUserLeaveHint");
                        invokeLifecycle(current, "onPause");
                        invokeLifecycle(current, "onStop");
                        current.foreground = false;
                    }
                    String tab = current != null ? current.tab : packageName;
                    boolean launched = ArtApkMain.launchActivity(tab, apkPath,
                        packageName, activityName, applicationName);
                    if (!launched) {
                        System.err.println("[DeviceController] failed to launch " + activityName);
                        if (current != null) focusRecord(current);
                    }
                } catch (Throwable t) {
                    System.err.println("[DeviceController] startActivityInCurrentTask error: " + t);
                }
            }
        });
    }

    public static void finishActivityByToken(final android.os.IBinder token) {
        android.os.Looper looper = android.os.Looper.getMainLooper();
        if (looper == null) return;
        android.os.Handler.createAsync(looper).post(new Runnable() {
            @Override public void run() {
                try {
                    ActivityRecord record = findActivityByToken(token);
                    if (record == null) {
                        record = activeRecord();
                    }
                    if (record != null) {
                        System.out.println("[DeviceController] finishActivityByToken: " + record.activityName);
                        finishAndRemoveActivity(record);
                    }
                } catch (Throwable t) {
                    System.err.println("[DeviceController] finishActivityByToken error: " + t);
                }
            }
        });
    }

    public static void finishActiveActivity() {
        android.os.Looper looper = android.os.Looper.getMainLooper();
        if (looper == null) return;
        android.os.Handler.createAsync(looper).post(new Runnable() {
            @Override public void run() {
                try {
                    ActivityRecord record = activeRecord();
                    if (record != null) {
                        System.out.println("[DeviceController] finishActiveActivity: " + record.activityName);
                        finishAndRemoveActivity(record);
                    }
                } catch (Throwable t) {
                    System.err.println("[DeviceController] finishActiveActivity error: " + t);
                }
            }
        });
    }

    private static void performBack() throws Exception {
        final ActivityRecord record = activeRecord();
        if (record == null || record.activity == null) return;
        if ("launcher".equals(record.tab)) {
            System.out.println("[DeviceController] back on launcher ignored");
            return;
        }
        try {
            java.lang.reflect.Method onBackPressed =
                record.activity.getClass().getMethod("onBackPressed");
            onBackPressed.setAccessible(true);
            onBackPressed.invoke(record.activity);
        } catch (Throwable t) {
            System.err.println("[DeviceController] onBackPressed error: " + t.getMessage());
        }
        finishAndRemoveActivity(record);
        System.out.println("[DeviceController] back dispatched");
    }

    private static void openAllApps(ActivityRecord launcher) {
        if (launcher == null || launcher.activity == null) return;
        try {
            Object activity = launcher.activity;
            ClassLoader loader = activity.getClass().getClassLoader();
            Class<?> launcherStateClass = Class.forName("com.android.launcher3.LauncherState", false, loader);
            java.lang.reflect.Field allAppsField = launcherStateClass.getField("ALL_APPS");
            Object allAppsState = allAppsField.get(null);

            java.lang.reflect.Method getStateManagerMethod = activity.getClass().getMethod("getStateManager");
            Object stateManager = getStateManagerMethod.invoke(activity);

            boolean invoked = false;
            for (java.lang.reflect.Method m : stateManager.getClass().getMethods()) {
                if ("goToState".equals(m.getName())) {
                    Class<?>[] params = m.getParameterTypes();
                    if (params.length == 2 && params[0].isAssignableFrom(launcherStateClass) && params[1] == boolean.class) {
                        m.invoke(stateManager, allAppsState, Boolean.FALSE);
                        invoked = true;
                        break;
                    } else if (params.length == 1 && params[0].isAssignableFrom(launcherStateClass)) {
                        m.invoke(stateManager, allAppsState);
                        invoked = true;
                    }
                }
            }
            if (!invoked) {
                try {
                    java.lang.reflect.Method showAll = activity.getClass().getMethod("showAllAppsFromIntent", boolean.class);
                    showAll.invoke(activity, Boolean.TRUE);
                    invoked = true;
                } catch (Throwable ignored) {
                }
            }
            System.out.println("[DeviceController] openAllApps invoked=" + invoked);
            focusRecord(launcher);
            scheduleFrame(launcher);
            MuplarFramePresenter.requestBurst();
        } catch (Throwable t) {
            System.err.println("[DeviceController] openAllApps error: " + t);
        }
    }

    private static void goToNormalState(ActivityRecord launcher) {
        if (launcher == null || launcher.activity == null) return;
        try {
            Object activity = launcher.activity;
            ClassLoader loader = activity.getClass().getClassLoader();
            Class<?> launcherStateClass = Class.forName("com.android.launcher3.LauncherState", false, loader);
            java.lang.reflect.Field normalField = launcherStateClass.getField("NORMAL");
            Object normalState = normalField.get(null);

            java.lang.reflect.Method getStateManagerMethod = activity.getClass().getMethod("getStateManager");
            Object stateManager = getStateManagerMethod.invoke(activity);

            boolean invoked = false;
            for (java.lang.reflect.Method m : stateManager.getClass().getMethods()) {
                if ("goToState".equals(m.getName())) {
                    Class<?>[] params = m.getParameterTypes();
                    if (params.length == 2 && params[0].isAssignableFrom(launcherStateClass) && params[1] == boolean.class) {
                        m.invoke(stateManager, normalState, Boolean.FALSE);
                        invoked = true;
                        break;
                    } else if (params.length == 1 && params[0].isAssignableFrom(launcherStateClass)) {
                        m.invoke(stateManager, normalState);
                        invoked = true;
                    }
                }
            }
            System.out.println("[DeviceController] goToNormalState invoked=" + invoked);
            focusRecord(launcher);
            scheduleFrame(launcher);
            MuplarFramePresenter.requestBurst();
        } catch (Throwable t) {
            System.err.println("[DeviceController] goToNormalState error: " + t);
        }
    }

    private static void toggleAllApps() {
        ActivityRecord launcher = activityForTab("launcher");
        if (launcher == null) {
            launcher = activeRecord();
        }
        if (launcher == null || launcher.activity == null) return;
        try {
            Object activity = launcher.activity;
            ClassLoader loader = activity.getClass().getClassLoader();
            Class<?> launcherStateClass = Class.forName("com.android.launcher3.LauncherState", false, loader);
            java.lang.reflect.Field allAppsField = launcherStateClass.getField("ALL_APPS");
            Object allAppsState = allAppsField.get(null);

            java.lang.reflect.Method getStateManagerMethod = activity.getClass().getMethod("getStateManager");
            Object stateManager = getStateManagerMethod.invoke(activity);
            java.lang.reflect.Method getStateMethod = stateManager.getClass().getMethod("getState");
            Object currentState = getStateMethod.invoke(stateManager);

            if (currentState == allAppsState) {
                goToNormalState(launcher);
            } else {
                openAllApps(launcher);
            }
        } catch (Throwable t) {
            System.err.println("[DeviceController] toggleAllApps error: " + t);
        }
    }

    private static void moveActiveActivityToBackground() throws Exception {
        ActivityRecord record = activeRecord();
        if (record == null || record.activity == null || !record.foreground)
            return;
        invokeLifecycle(record, "onUserLeaveHint");
        invokeLifecycle(record, "onPause");
        invokeLifecycle(record, "onStop");
        record.foreground = false;
        System.out.println("[DeviceController] activity backgrounded");
    }

    private static void pushTaskStack(String tab) {
        synchronized (lock) {
            activityStack.remove(tab);
            activityStack.add(tab);
        }
    }

    private static String popTaskStack(String tab) {
        synchronized (lock) {
            activityStack.remove(tab);
            if (!activityStack.isEmpty()) {
                return activityStack.get(activityStack.size() - 1);
            }
            return activities.containsKey("launcher") ? "launcher" : "";
        }
    }

    private static void toggleRecents() throws Exception {
        synchronized (lock) {
            System.out.println("[DeviceController] recents taskStack=" + taskStack);
            if (taskStack.size() >= 2) {
                TaskRecord previousTask = taskStack.get(taskStack.size() - 2);
                ActivityRecord top = previousTask.topActivity();
                if (top != null) {
                    focusRecord(top);
                    return;
                }
            }
            TaskRecord launcherTask = tasksByTab.get("launcher");
            if (launcherTask != null && launcherTask.topActivity() != null) {
                focusRecord(launcherTask.topActivity());
            }
        }
    }

    private static void focusRecord(ActivityRecord record) throws Exception {
        if (record == null || record.activity == null) return;
        TaskRecord task = record.task;
        if (task != null) {
            synchronized (lock) {
                taskStack.remove(task);
                taskStack.add(task);
            }
        }
        pushTaskStack(record.tab);
        ActivityRecord previous = activeRecord();
        if (previous != null && previous != record)
            moveActiveActivityToBackground();
        activeTab = record.tab;
        if (record.foreground) return;
        invokeLifecycle(record, "onRestart");
        invokeLifecycle(record, "onStart");
        invokeLifecycle(record, "onResume");
        record.foreground = true;
        scheduleFrame(record);
        System.out.println("[DeviceController] activity resumed tab=" +
            record.tab + " activity=" + record.activityName);
    }

    private static void finishTab(String tabValue) throws Exception {
        TaskRecord task;
        synchronized (lock) {
            task = tasksByTab.get(clean(tabValue));
        }
        if (task != null) {
            ArrayList<ActivityRecord> stackCopy = new ArrayList<ActivityRecord>(task.activityStack);
            for (int i = stackCopy.size() - 1; i >= 0; i--) {
                ActivityRecord rec = stackCopy.get(i);
                finishRecord(rec);
            }
            String newActiveTab;
            synchronized (lock) {
                for (ActivityRecord rec : stackCopy) {
                    if (rec.token != null) recordsByToken.remove(rec.token);
                }
                task.activityStack.clear();
                tasks.remove(task.taskId);
                tasksByTab.remove(task.tab);
                taskStack.remove(task);
                activities.remove(task.tab);
                activityStack.remove(task.tab);
                TaskRecord prev = !taskStack.isEmpty() ? taskStack.get(taskStack.size() - 1) : tasksByTab.get("launcher");
                newActiveTab = prev != null ? prev.tab : (activities.containsKey("launcher") ? "launcher" : "");
                if (tabValue.equals(activeTab)) {
                    activeTab = newActiveTab;
                }
            }
            if (!"launcher".equals(tabValue)) {
                System.out.println("[DeviceController] finishTab: tab-finished for " + tabValue);
                FrameworkServiceClient.request("tab-finished", tabValue);
            }
            ActivityRecord next = activityForTab(newActiveTab);
            if (next != null) {
                focusRecord(next);
            } else {
                MuplarFramePresenter.clear();
            }
            return;
        }
        ActivityRecord record = activityForTab(tabValue);
        if (record == null) return;
        finishAndRemoveActivity(record);
    }

    private static void finishRecord(ActivityRecord record) throws Exception {
        if (record == null || record.activity == null || record.finishing) return;
        record.finishing = true;
        try {
            java.lang.reflect.Method finish =
                record.activity.getClass().getMethod("finish");
            finish.setAccessible(true);
            finish.invoke(record.activity);
        } catch (NoSuchMethodException ignored) {
        }
        if (record.foreground) {
            invokeLifecycle(record, "onPause");
            invokeLifecycle(record, "onStop");
        }
        invokeLifecycle(record, "onDestroy");
        record.foreground = false;
        record.activity = null;
        record.activityClass = null;
        System.out.println("[DeviceController] activity finished tab=" +
            record.tab + " activity=" + record.activityName);
    }

    private static void finishAndRemoveActivity(ActivityRecord record) throws Exception {
        if (record == null) return;
        TaskRecord task = record.task;

        finishRecord(record);

        synchronized (lock) {
            if (record.token != null) {
                recordsByToken.remove(record.token);
            }
            if (task != null) {
                task.removeActivity(record);
            }
        }

        if (task != null && !task.isEmpty()) {
            ActivityRecord newTop = task.topActivity();
            synchronized (lock) {
                activities.put(task.tab, newTop);
            }
            System.out.println("[DeviceController] task " + task.taskId +
                " (" + task.tab + ") still active with " + task.size() +
                " activities, resuming top=" + newTop.activityName);
            focusRecord(newTop);
        } else {
            String tabValue = record.tab;
            boolean wasActive;
            String newActiveTab;
            synchronized (lock) {
                activities.remove(tabValue);
                if (task != null) {
                    tasks.remove(task.taskId);
                    tasksByTab.remove(task.tab);
                    taskStack.remove(task);
                }
                wasActive = tabValue.equals(activeTab);
                newActiveTab = popTaskStack(tabValue);
                if (newActiveTab.isEmpty()) {
                    TaskRecord prev = !taskStack.isEmpty() ? taskStack.get(taskStack.size() - 1) : tasksByTab.get("launcher");
                    newActiveTab = prev != null ? prev.tab : (activities.containsKey("launcher") ? "launcher" : "");
                }
                if (wasActive) {
                    activeTab = newActiveTab;
                }
            }
            if (!"launcher".equals(tabValue)) {
                System.out.println("[DeviceController] task empty, requesting tab-finished for " + tabValue);
                FrameworkServiceClient.request("tab-finished", tabValue);
            }
            if (wasActive) {
                ActivityRecord next = activityForTab(newActiveTab);
                if (next != null) {
                    focusRecord(next);
                } else {
                    MuplarFramePresenter.clear();
                }
            }
        }
    }

    public static ActivityRecord activeRecord() {
        synchronized (lock) {
            TaskRecord task = tasksByTab.get(activeTab);
            if (task != null && task.topActivity() != null) {
                return task.topActivity();
            }
            ActivityRecord record = activities.get(activeTab);
            if (record != null) {
                return record;
            }
            if (!taskStack.isEmpty()) {
                TaskRecord topTask = taskStack.get(taskStack.size() - 1);
                if (topTask != null && topTask.topActivity() != null) {
                    return topTask.topActivity();
                }
            }
            if (activities.containsKey("launcher")) {
                return activities.get("launcher");
            }
            if (!activities.isEmpty()) {
                return activities.values().iterator().next();
            }
            return null;
        }
    }

    private static void scheduleFrame(ActivityRecord record) {
        if (record == null || record.activity == null) {
            return;
        }
        try {
            java.lang.reflect.Method getWindow =
                record.activity.getClass().getMethod("getWindow");
            Object window = getWindow.invoke(record.activity);
            java.lang.reflect.Method getDecorView =
                window.getClass().getMethod("getDecorView");
            Object decor = getDecorView.invoke(window);
            if (decor instanceof android.view.View) {
                MuplarFramePresenter.schedule((android.view.View) decor);
            }
        } catch (Throwable ignored) {
        }
    }

    public static ActivityRecord activityForTab(String tabValue) {
        synchronized (lock) {
            TaskRecord task = tasksByTab.get(clean(tabValue));
            if (task != null && task.topActivity() != null) {
                return task.topActivity();
            }
            return activities.get(clean(tabValue));
        }
    }

    private static boolean hasActivityForTab(String tabValue) {
        return activityForTab(tabValue) != null;
    }

    private static boolean isFinishing(Object target) {
        try {
            java.lang.reflect.Method method = target.getClass().getMethod(
                "isFinishing");
            Object value = method.invoke(target);
            return Boolean.TRUE.equals(value);
        } catch (Throwable ignored) {
            return false;
        }
    }

    private static void invokeLifecycle(ActivityRecord record, String name)
        throws Exception {
        Class<?> type = record.activityClass;
        if (type == null && record.activity != null)
            type = record.activity.getClass();
        while (type != null) {
            try {
                java.lang.reflect.Method method = type.getDeclaredMethod(name);
                method.setAccessible(true);
                method.invoke(record.activity);
                return;
            } catch (NoSuchMethodException ignored) {
                type = type.getSuperclass();
            }
        }
    }

    private static boolean sameActivity(String packageValue,
                                        String activityValue,
                                        String nextPackage,
                                        String nextActivity) {
        String packageName = clean(packageValue);
        String requestedPackage = clean(nextPackage);
        return !packageName.isEmpty() &&
            packageName.equals(requestedPackage) &&
            normalizeActivityName(packageName, activityValue).equals(
                normalizeActivityName(requestedPackage, nextActivity));
    }

    private static String defaultTabFor(String packageValue,
                                        String activityValue) {
        String packageName = clean(packageValue);
        String activityName = normalizeActivityName(packageName, activityValue);
        if (packageName.toLowerCase().contains("launcher"))
            return "launcher";
        return packageName + "/" + activityName;
    }

    private static boolean isHostTab(String value) {
        return "launcher".equals(value) || "settings".equals(value);
    }

    private static String normalizeActivityName(String packageValue,
                                                String activityValue) {
        String packageName = clean(packageValue);
        String activityName = clean(activityValue);
        if (activityName.startsWith("."))
            return packageName + activityName;
        if (!packageName.isEmpty() && activityName.indexOf('.') < 0)
            return packageName + "." + activityName;
        return activityName;
    }

    private static String clean(String value) {
        return value == null ? "" : value;
    }
}
