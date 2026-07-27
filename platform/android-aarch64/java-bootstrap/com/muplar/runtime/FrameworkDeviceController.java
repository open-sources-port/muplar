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

    private static volatile Process subscriber;
    private static volatile long generation;
    private static volatile String activeTab = "launcher";
    private static volatile boolean mainReady;

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
            Thread reader = new Thread(new Runnable() {
                @Override public void run() {
                    readActions(subscriber);
                }
            }, "muplar-device-actions");
            reader.setDaemon(true);
            reader.start();
            Runtime.getRuntime().addShutdownHook(new Thread(new Runnable() {
                @Override public void run() {
                    Process process = subscriber;
                    if (process != null) process.destroy();
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

    private static long parseLong(String value, long fallback) {
        try {
            return Long.parseLong(value);
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
            }
        });
    }

    private static void applyActionOnMain(DeviceAction next) {
        try {
            if (isStale(next)) return;
            if ("back".equals(next.action)) {
                performBack();
            } else if ("home".equals(next.action)) {
                moveActiveActivityToBackground();
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
