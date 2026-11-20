package com.muplar.notificationsmoke;

import android.app.Activity;
import android.app.NotificationChannel;
import android.app.NotificationChannelGroup;
import android.app.NotificationManager;
import android.os.Bundle;
import java.io.File;
import java.io.FileOutputStream;
import java.util.Objects;

public final class MainActivity extends Activity {
    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        NotificationManager manager = getSystemService(NotificationManager.class);
        File marker = new File(getFilesDir(), "persisted-channel");
        if (marker.exists()) {
            NotificationChannel persisted = manager.getNotificationChannel("persist");
            require(persisted != null && getPackageName().contentEquals(persisted.getName()),
                "channel did not persist or leaked from another package");
            require(manager.getNotificationChannels().size() == 1, "unexpected persistent channels");
            manager.deleteNotificationChannel("persist");
            require(manager.getNotificationChannels().isEmpty(), "delete not reflected in query");
            require(marker.delete(), "cannot clear test phase");
            System.out.println("NOTIFICATION_CHANNELS_SMOKE_PASS phase=persist package=" + getPackageName());
            return;
        }
        require(manager.getNotificationChannels().isEmpty(), "new package sees foreign channels");
        require(manager.getNotificationChannelGroups().isEmpty(), "new package sees foreign groups");
        manager.createNotificationChannelGroup(new NotificationChannelGroup("updates", "Updates"));
        NotificationChannel original = new NotificationChannel("shared", "First", NotificationManager.IMPORTANCE_LOW);
        original.setGroup("updates");
        original.setDescription("Original description");
        manager.createNotificationChannel(original);
        original.setName("Caller mutated input");
        NotificationChannel stored = manager.getNotificationChannel("shared");
        require("First".contentEquals(stored.getName()), "input aliases stored channel");
        stored.setDescription("Caller mutated output");
        require("Original description".equals(manager.getNotificationChannel("shared").getDescription()),
            "output aliases stored channel");
        require(manager.getNotificationChannelGroup("updates").getChannels().size() == 1,
            "group channel membership missing");
        NotificationChannel changed = new NotificationChannel("shared", "Renamed", NotificationManager.IMPORTANCE_HIGH);
        changed.setDescription("Updated description");
        changed.setSound(null, null);
        manager.createNotificationChannel(changed);
        NotificationChannel updated = manager.getNotificationChannel("shared");
        require("Renamed".contentEquals(updated.getName()), "duplicate creation did not update name");
        require(updated.getImportance() == NotificationManager.IMPORTANCE_LOW, "duplicate creation raised importance");
        require(Objects.equals(updated.getSound(), stored.getSound()), "duplicate creation changed sound");
        manager.createNotificationChannel(new NotificationChannel("shared", "Lowered", NotificationManager.IMPORTANCE_MIN));
        require(manager.getNotificationChannel("shared").getImportance() == NotificationManager.IMPORTANCE_MIN,
            "importance was not lowered");
        manager.deleteNotificationChannel("shared");
        require(manager.getNotificationChannel("shared") == null, "deleted channel still visible");
        manager.createNotificationChannel(changed);
        require(manager.getNotificationChannel("shared").getImportance() == NotificationManager.IMPORTANCE_MIN,
            "deleted channel lost original behavior on recreation");
        manager.deleteNotificationChannelGroup("updates");
        require(manager.getNotificationChannelGroups().isEmpty(), "group deletion failed");
        require(manager.getNotificationChannels().isEmpty(), "group deletion left live channels");
        try {
            manager.createNotificationChannel(new NotificationChannel("invalid", "", NotificationManager.IMPORTANCE_LOW));
            throw new AssertionError("invalid channel was accepted");
        } catch (IllegalArgumentException expected) {
        }
        manager.createNotificationChannel(new NotificationChannel("persist", getPackageName(), NotificationManager.IMPORTANCE_LOW));
        try (FileOutputStream out = new FileOutputStream(marker)) { out.write(1); }
        catch (Exception error) { throw new AssertionError(error); }
        System.out.println("NOTIFICATION_CHANNELS_SMOKE_PASS phase=create package=" + getPackageName());
    }

    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
}
