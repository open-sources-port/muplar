package com.muplar.runtime;

import java.awt.BorderLayout;
import java.awt.Component;
import java.awt.Container;
import java.awt.Frame;
import java.util.Collections;
import java.util.Map;
import java.util.WeakHashMap;
import javax.swing.AbstractButton;
import javax.swing.BoxLayout;
import javax.swing.JButton;
import javax.swing.JCheckBox;
import javax.swing.JFrame;
import javax.swing.JLabel;
import javax.swing.JPanel;

public final class HostUi {
    private static final Map<Object, JFrame> windows =
        Collections.synchronizedMap(new WeakHashMap<Object, JFrame>());

    private HostUi() {}

    public static Object createLinearLayout() { return new JPanel(); }
    public static Object createTextView() { return new JLabel(); }
    public static Object createButton() { return new JButton(); }
    public static Object createCheckBox() { return new JCheckBox(); }

    public static void setLinearLayoutOrientation(Object peer, int orientation) {
        JPanel panel = (JPanel) peer;
        panel.setLayout(new BoxLayout(panel,
            orientation == 0 ? BoxLayout.X_AXIS : BoxLayout.Y_AXIS));
    }

    public static void addChild(Object parent, Object child) {
        ((Container) parent).add((Component) child);
    }

    public static void setText(Object peer, String text) {
        if (peer instanceof JLabel) ((JLabel) peer).setText(text);
        else if (peer instanceof AbstractButton) ((AbstractButton) peer).setText(text);
    }

    public static void setEnabled(Object peer, boolean enabled) {
        ((Component) peer).setEnabled(enabled);
    }

    public static void setOnClickListener(Object peer, final Runnable listener) {
        AbstractButton button = (AbstractButton) peer;
        for (java.awt.event.ActionListener old : button.getActionListeners())
            button.removeActionListener(old);
        if (listener != null) button.addActionListener(event -> listener.run());
    }

    public static boolean isChecked(Object peer) {
        return ((AbstractButton) peer).isSelected();
    }

    public static void setChecked(Object peer, boolean checked) {
        ((AbstractButton) peer).setSelected(checked);
    }

    public static void showActivity(final Object activity,
                                    final String title,
                                    final Object content) {
        JFrame old = windows.remove(activity);
        if (old != null) old.dispose();
        JFrame frame = new JFrame(title);
        frame.setDefaultCloseOperation(JFrame.DISPOSE_ON_CLOSE);
        frame.setLayout(new BorderLayout());
        if (content instanceof Component)
            frame.add((Component) content, BorderLayout.CENTER);
        frame.pack();
        frame.setSize(Math.max(frame.getWidth(), 420),
                      Math.max(frame.getHeight(), 280));
        frame.setLocationByPlatform(true);
        windows.put(activity, frame);
        frame.setVisible(true);
        frame.toFront();
        frame.requestFocus();
    }

    public static void finishActivity(final Object activity) {
        JFrame frame = windows.remove(activity);
        if (frame != null) frame.dispose();
    }

    public static boolean hasVisibleWindows() {
        for (Frame frame : Frame.getFrames()) {
            if (frame.isDisplayable() && frame.isVisible()) return true;
        }
        return false;
    }
}
