package com.muplar.runtime;

import android.app.NotificationChannel;
import android.app.NotificationChannelGroup;
import android.app.NotificationManager;
import android.os.Parcel;
import android.os.Parcelable;
import android.text.TextUtils;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.lang.reflect.Method;
import java.nio.channels.FileLock;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Prefix-local, package-scoped channel metadata. Does not deliver notifications. */
final class MuplarNotificationChannels {
    private static final int MAGIC = 0x4d4e4331;
    private static final int MAX_RECORDS = 1000;
    private static final int MAX_FILE_BYTES = 16 * 1024 * 1024;
    private static final int MAX_RECORD_BYTES = 1024 * 1024;

    private static final class Entry {
        byte[] data;
        boolean deleted;
        Entry(byte[] data, boolean deleted) { this.data = data; this.deleted = deleted; }
    }

    private static final class State {
        final Map<String, Entry> channels = new LinkedHashMap<>();
        final Map<String, Entry> groups = new LinkedHashMap<>();
    }

    private MuplarNotificationChannels() {}

    static boolean handles(String name) {
        return "createNotificationChannels".equals(name) || "getNotificationChannels".equals(name) ||
            "getNotificationChannel".equals(name) || "deleteNotificationChannel".equals(name) ||
            "getConversationNotificationChannel".equals(name) ||
            "createNotificationChannelGroups".equals(name) ||
            "getNotificationChannelGroups".equals(name) || "getNotificationChannelGroup".equals(name) ||
            "deleteNotificationChannelGroup".equals(name);
    }

    static synchronized Object invoke(Method method, Object[] args) throws Exception {
        String name = method.getName();
        String pkg = (String) args[0];
        if (pkg == null || !pkg.matches("[A-Za-z0-9_]+(?:\\.[A-Za-z0-9_]+)*")) {
            throw new IllegalArgumentException("Invalid notification package");
        }
        if ("getNotificationChannels".equals(name)) {
            samePackage(pkg, (String) args[1]);
            userZero(args[2]);
        } else if ("getNotificationChannel".equals(name) ||
                   "getConversationNotificationChannel".equals(name)) {
            userZero(args[1]);
            samePackage(pkg, (String) args[2]);
        }
        File directory = new File("/data/system/muplar/notification-channels/u0");
        if (!directory.isDirectory() && !directory.mkdirs()) {
            throw new IllegalStateException("Cannot create notification channel storage");
        }
        File file = new File(directory, pkg + ".bin");
        try (RandomAccessFile lockFile = new RandomAccessFile(new File(directory, pkg + ".lock"), "rw");
             FileLock lock = lockFile.getChannel().lock()) {
            State state = load(file);
            if ("createNotificationChannels".equals(name)) {
                for (Object item : sliceItems(args[1])) {
                    NotificationChannel incoming = copy((NotificationChannel) item, NotificationChannel.CREATOR);
                    if (TextUtils.isEmpty(incoming.getId()) || TextUtils.isEmpty(incoming.getName()) ||
                        incoming.getImportance() < NotificationManager.IMPORTANCE_NONE ||
                        incoming.getImportance() > NotificationManager.IMPORTANCE_MAX) {
                        throw new IllegalArgumentException("Invalid notification channel");
                    }
                    Entry old = state.channels.get(incoming.getId());
                    if (old != null) {
                        NotificationChannel existing = decode(old.data, NotificationChannel.CREATOR);
                        existing.setName(incoming.getName());
                        existing.setDescription(incoming.getDescription());
                        // Re-creation restores the original behavior. Ordinary duplicate
                        // creation can lower importance but cannot raise it or change sound.
                        if (!old.deleted && incoming.getImportance() < existing.getImportance()) {
                            existing.setImportance(incoming.getImportance());
                        }
                        if (existing.getGroup() == null) existing.setGroup(incoming.getGroup());
                        incoming = existing;
                    }
                    if (incoming.getGroup() != null && !state.groups.containsKey(incoming.getGroup())) {
                        throw new IllegalArgumentException("Notification channel group does not exist");
                    }
                    state.channels.put(incoming.getId(), new Entry(encode(incoming), false));
                }
                save(file, state);
                return null;
            }
            if ("createNotificationChannelGroups".equals(name)) {
                for (Object item : sliceItems(args[1])) {
                    NotificationChannelGroup group = (NotificationChannelGroup) item;
                    if (TextUtils.isEmpty(group.getId()) || TextUtils.isEmpty(group.getName())) {
                        throw new IllegalArgumentException("Invalid notification channel group");
                    }
                    state.groups.put(group.getId(), new Entry(encode(group), false));
                }
                save(file, state);
                return null;
            }
            if ("deleteNotificationChannel".equals(name)) {
                Entry entry = state.channels.get((String) args[1]);
                if (entry != null) entry.deleted = true;
                save(file, state);
                return null;
            }
            if ("deleteNotificationChannelGroup".equals(name)) {
                String id = (String) args[1];
                state.groups.remove(id);
                for (Entry entry : state.channels.values()) {
                    if (id.equals(decode(entry.data, NotificationChannel.CREATOR).getGroup())) entry.deleted = true;
                }
                save(file, state);
                return null;
            }
            if ("getNotificationChannels".equals(name)) return slice(channels(state));
            if ("getNotificationChannel".equals(name)) return channel(state, (String) args[3]);
            if ("getConversationNotificationChannel".equals(name)) {
                String parent = (String) args[3];
                String conversation = (String) args[5];
                for (NotificationChannel channel : channels(state)) {
                    if (parent.equals(channel.getParentChannelId()) && conversation != null &&
                        conversation.equals(channel.getConversationId())) return channel;
                }
                return Boolean.TRUE.equals(args[4]) ? channel(state, parent) : null;
            }
            List<NotificationChannelGroup> groups = new ArrayList<>();
            for (Entry entry : state.groups.values()) {
                NotificationChannelGroup group = decode(entry.data, NotificationChannelGroup.CREATOR);
                // Do not trust a caller-supplied embedded channel list.
                group.getChannels().clear();
                for (NotificationChannel channel : channels(state)) {
                    if (group.getId().equals(channel.getGroup())) group.getChannels().add(channel);
                }
                if ("getNotificationChannelGroup".equals(name) && group.getId().equals(args[1])) return group;
                groups.add(group);
            }
            return "getNotificationChannelGroup".equals(name) ? null : slice(groups);
        }
    }

    private static void samePackage(String caller, String target) {
        if (!caller.equals(target)) throw new SecurityException("Notification delegation is not implemented");
    }

    private static void userZero(Object user) {
        if (((Number) user).intValue() != 0) throw new UnsupportedOperationException("Only notification user 0 is supported");
    }

    private static NotificationChannel channel(State state, String id) {
        Entry entry = state.channels.get(id);
        return entry == null || entry.deleted ? null : decode(entry.data, NotificationChannel.CREATOR);
    }

    private static List<NotificationChannel> channels(State state) {
        List<NotificationChannel> result = new ArrayList<>();
        for (Entry entry : state.channels.values()) {
            if (!entry.deleted) result.add(decode(entry.data, NotificationChannel.CREATOR));
        }
        return result;
    }

    private static List<?> sliceItems(Object value) throws Exception {
        return (List<?>) value.getClass().getMethod("getList").invoke(value);
    }

    private static Object slice(List<?> values) throws Exception {
        return Class.forName("android.content.pm.ParceledListSlice")
            .getConstructor(List.class).newInstance(values);
    }

    private static byte[] encode(Parcelable value) {
        Parcel parcel = Parcel.obtain();
        try {
            value.writeToParcel(parcel, 0);
            byte[] bytes = parcel.marshall();
            if (bytes.length > MAX_RECORD_BYTES) throw new IllegalArgumentException("Notification channel too large");
            return bytes;
        } finally { parcel.recycle(); }
    }

    private static <T> T decode(byte[] bytes, Parcelable.Creator<T> creator) {
        Parcel parcel = Parcel.obtain();
        try {
            parcel.unmarshall(bytes, 0, bytes.length);
            parcel.setDataPosition(0);
            return creator.createFromParcel(parcel);
        } finally { parcel.recycle(); }
    }

    private static <T extends Parcelable> T copy(T value, Parcelable.Creator<T> creator) {
        return decode(encode(value), creator);
    }

    private static State load(File file) throws Exception {
        State state = new State();
        if (!file.exists()) return state;
        if (file.length() > MAX_FILE_BYTES) throw new IllegalStateException("Notification channel file too large");
        try (DataInputStream input = new DataInputStream(new FileInputStream(file))) {
            if (input.readInt() != MAGIC || input.readInt() != android.os.Build.VERSION.SDK_INT) {
                throw new IllegalStateException("Notification channel storage version mismatch");
            }
            read(input, state.channels);
            read(input, state.groups);
        }
        return state;
    }

    private static void read(DataInputStream input, Map<String, Entry> values) throws Exception {
        int count = input.readInt();
        if (count < 0 || count > MAX_RECORDS) throw new IllegalStateException("Invalid notification channel count");
        for (int i = 0; i < count; i++) {
            String id = input.readUTF();
            boolean deleted = input.readBoolean();
            int length = input.readInt();
            if (length < 0 || length > MAX_RECORD_BYTES) throw new IllegalStateException("Invalid channel record size");
            byte[] bytes = new byte[length];
            input.readFully(bytes);
            values.put(id, new Entry(bytes, deleted));
        }
    }

    private static void save(File file, State state) throws Exception {
        if (state.channels.size() > MAX_RECORDS || state.groups.size() > MAX_RECORDS) {
            throw new IllegalArgumentException("Too many notification channels");
        }
        long size = 16;
        for (Map<String, Entry> values : java.util.Arrays.asList(state.channels, state.groups)) {
            for (Map.Entry<String, Entry> item : values.entrySet()) {
                size += item.getValue().data.length + 7L + item.getKey().length() * 3L;
            }
        }
        if (size > MAX_FILE_BYTES) throw new IllegalArgumentException("Notification channel file too large");
        // Avoid vendor AtomicFile hooks. The sibling temp file is fsynced and
        // renamed on the same filesystem while the package lock is held.
        File temporary = new File(file + ".new");
        boolean committed = false;
        try {
            try (FileOutputStream stream = new FileOutputStream(temporary)) {
                DataOutputStream output = new DataOutputStream(stream);
                output.writeInt(MAGIC);
                output.writeInt(android.os.Build.VERSION.SDK_INT);
                write(output, state.channels);
                write(output, state.groups);
                output.flush();
                stream.getFD().sync();
            }
            if (!temporary.renameTo(file)) throw new IOException("Cannot commit notification channel metadata");
            committed = true;
        } finally {
            if (!committed) temporary.delete();
        }
    }

    private static void write(DataOutputStream output, Map<String, Entry> values) throws Exception {
        output.writeInt(values.size());
        for (Map.Entry<String, Entry> item : values.entrySet()) {
            output.writeUTF(item.getKey());
            output.writeBoolean(item.getValue().deleted);
            output.writeInt(item.getValue().data.length);
            output.write(item.getValue().data);
        }
    }
}
