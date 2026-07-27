package com.muplar.runtime;

import java.io.BufferedReader;
import java.io.File;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.Map;

public final class FrameworkDeviceController {
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

    private static final class ActivityRecord {
        String tab;
        String apkPath;
        String packageName;
        String activityName;
        Object activity;
        Class<?> activityClass;
        boolean foreground;
    }

    private static final Object lock = new Object();
    private static final Map<String, ActivityRecord> activities =
        new LinkedHashMap<String, ActivityRecord>();
    private static final ArrayList<DeviceAction> pendingActions =
        new ArrayList<DeviceAction>();
    private static final ArrayList<DeviceInput> pendingInputs =
        new ArrayList<DeviceInput>();

    private static volatile Process subscriber;
    private static volatile Process inputSubscriber;
    private static volatile long generation;
    private static volatile long inputGeneration;
    private static volatile String activeTab = "launcher";
    private static volatile boolean mainReady;
    private static volatile long pointerDownTime;

    private FrameworkDeviceController() {}

    public static void registerActivity(Object value,
                                        String packageValue,
                                        String activityValue,
                                        Class<?> type) {
        registerActivityForTab(defaultTabFor(packageValue, activityValue),
            "", packageValue, activityValue, value, type);
    }

    public static void registerActivityForTab(String tabValue,
                                             String apkValue,
                                             String packageValue,
                                             String activityValue,
                                             Object value,
                                             Class<?> type) {
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
        synchronized (lock) {
            activities.put(normalizedTab, record);
            activeTab = normalizedTab;
            mainReady = true;
        }
        System.out.println("[DeviceController] registered activity tab=" +
            normalizedTab + " package=" + normalizedPackage + " activity=" +
            normalizedActivity);
        flushPendingActions();
    }

    public static synchronized void start() {
        if (subscriber != null) return;
        String executable = System.getProperty("muplar.service.executable", "");
        String socket = System.getProperty("muplar.service.socket", "");
        if (executable.isEmpty() || socket.isEmpty() ||
            !new File(executable).isFile()) return;
        try {
            subscriber = new ProcessBuilder(executable, "--socket", socket,
                "--client", "subscribe-device-actions")
                .redirectError(ProcessBuilder.Redirect.INHERIT).start();
            inputSubscriber = new ProcessBuilder(executable, "--socket", socket,
                "--client", "subscribe-device-inputs")
                .redirectError(ProcessBuilder.Redirect.INHERIT).start();
            Thread reader = new Thread(new Runnable() {
                @Override public void run() {
                    readActions(subscriber);
                }
            }, "muplar-device-actions");
            reader.setDaemon(true);
            reader.start();
            Thread inputReader = new Thread(new Runnable() {
                @Override public void run() {
                    readInputs(inputSubscriber);
                }
            }, "muplar-device-inputs");
            inputReader.setDaemon(true);
            inputReader.start();
            Runtime.getRuntime().addShutdownHook(new Thread(new Runnable() {
                @Override public void run() {
                    Process process = subscriber;
                    if (process != null) process.destroy();
                    Process inputProcess = inputSubscriber;
                    if (inputProcess != null) inputProcess.destroy();
                }
            }, "muplar-device-actions-shutdown"));
            System.out.println("[DeviceController] subscribed");
        } catch (Exception error) {
            System.err.println("[DeviceController] subscribe failed: " + error);
        }
    }

    private static void readActions(Process process) {
        try (BufferedReader input = new BufferedReader(
                 new InputStreamReader(process.getInputStream(), "UTF-8"))) {
            String line;
            long nextGeneration = generation;
            String nextAction = "";
            String nextTab = "";
            String nextApk = "";
            String nextPackage = "";
            String nextActivity = "";
            String nextApplication = "";
            while ((line = input.readLine()) != null) {
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
                    applyAction(new DeviceAction(nextGeneration, nextAction,
                        nextTab, nextApk, nextPackage, nextActivity,
                        nextApplication));
                }
            }
        } catch (Exception error) {
            System.err.println("[DeviceController] stream failed: " + error);
        } finally {
            subscriber = null;
        }
    }

    private static void readInputs(Process process) {
        try (BufferedReader input = new BufferedReader(
                 new InputStreamReader(process.getInputStream(), "UTF-8"))) {
            String line;
            long nextGeneration = inputGeneration;
            String nextTab = "";
            int nextType = 0;
            int nextAction = 0;
            int nextSource = 0;
            int nextDeviceId = 0;
            int nextKeyCode = 0;
            float nextX = 0.0f;
            float nextY = 0.0f;
            while ((line = input.readLine()) != null) {
                if (line.startsWith("generation=")) {
                    nextGeneration =
                        parseLong(line.substring(11), inputGeneration);
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
                    applyInput(new DeviceInput(nextGeneration, nextTab,
                        nextType, nextAction, nextSource, nextDeviceId,
                        nextKeyCode, nextX, nextY));
                }
            }
        } catch (Exception error) {
            System.err.println("[DeviceController] input stream failed: " +
                error);
        } finally {
            inputSubscriber = null;
        }
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
            if (looper == null) {
                queueAction(next);
                return;
            }
            new android.os.Handler(looper).post(new Runnable() {
                @Override public void run() {
                    if (!isReadyFor(next)) {
                        queueAction(next);
                        return;
                    }
                    applyActionOnMain(next);
                    flushPendingActions();
                }
            });
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
            new android.os.Handler(looper).post(new Runnable() {
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
        new android.os.Handler(looper).post(new Runnable() {
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
        new android.os.Handler(looper).post(new Runnable() {
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
            if (isStale(next)) return;
            if ("back".equals(next.action)) {
                performBack();
            } else if ("home".equals(next.action)) {
                ActivityRecord launcher = activityForTab("launcher");
                if (launcher != null) {
                    focusRecord(launcher);
                } else {
                    moveActiveActivityToBackground();
                }
            } else if ("recents".equals(next.action)) {
                System.out.println("[DeviceController] recents tab=" + next.tab);
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
        }
        android.view.MotionEvent event = android.view.MotionEvent.obtain(
            pointerDownTime, now, input.action, input.x, input.y, 0);
        event.setSource(input.source);
        try {
            if (!invokeInputDispatch(record.activity, "dispatchTouchEvent",
                    android.view.MotionEvent.class, event)) {
                dispatchToDecorView(record.activity, "dispatchTouchEvent",
                    android.view.MotionEvent.class, event);
            }
        } finally {
            event.recycle();
            if (input.action == android.view.MotionEvent.ACTION_UP ||
                input.action == android.view.MotionEvent.ACTION_CANCEL) {
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
            java.lang.reflect.Method method =
                target.getClass().getMethod(methodName, eventClass);
            method.setAccessible(true);
            Object result = method.invoke(target, event);
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
        synchronized (lock) {
            return next.generation < generation;
        }
    }

    private static void focusOrLaunchActivity(DeviceAction next)
        throws Exception {
        ActivityRecord record = activityForTab(next.tab);
        if (record != null &&
            sameActivity(record.packageName, record.activityName,
                next.packageName, next.activityName)) {
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

    private static void performBack() throws Exception {
        ActivityRecord record = activeRecord();
        if (record == null || record.activity == null) return;
        try {
            java.lang.reflect.Method onBackPressed =
                record.activity.getClass().getMethod("onBackPressed");
            onBackPressed.setAccessible(true);
            onBackPressed.invoke(record.activity);
            if (isFinishing(record.activity)) {
                finishRecord(record);
                removeRecord(record);
            }
            System.out.println("[DeviceController] back dispatched");
            return;
        } catch (NoSuchMethodException ignored) {
        }
        finishRecord(record);
        removeRecord(record);
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

    private static void focusRecord(ActivityRecord record) throws Exception {
        if (record == null || record.activity == null) return;
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
            record.tab);
    }

    private static void finishTab(String tabValue) throws Exception {
        ActivityRecord record = activityForTab(tabValue);
        if (record == null) return;
        finishRecord(record);
        removeRecord(record);
    }

    private static void finishRecord(ActivityRecord record) throws Exception {
        if (record == null || record.activity == null) return;
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
            record.tab);
    }

    private static void removeRecord(ActivityRecord record) {
        synchronized (lock) {
            activities.remove(record.tab);
            if (record.tab.equals(activeTab)) {
                activeTab = activities.containsKey("launcher")
                    ? "launcher"
                    : "";
            }
        }
    }

    private static ActivityRecord activeRecord() {
        synchronized (lock) {
            return activities.get(activeTab);
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

    private static ActivityRecord activityForTab(String tabValue) {
        synchronized (lock) {
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
