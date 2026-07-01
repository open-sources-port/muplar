package com.muplar.runtime;

import java.awt.BorderLayout;
import java.awt.Component;
import java.awt.Container;
import java.awt.Frame;
import java.awt.Image;
import java.awt.event.ComponentAdapter;
import java.awt.event.ComponentEvent;
import java.awt.event.WindowAdapter;
import java.awt.event.WindowEvent;
import java.awt.event.WindowFocusListener;
import java.util.Collections;
import java.util.Map;
import java.util.WeakHashMap;
import javax.swing.AbstractButton;
import javax.swing.BoxLayout;
import javax.swing.JButton;
import javax.swing.JCheckBox;
import javax.swing.JFrame;
import javax.swing.ImageIcon;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.SwingUtilities;
import android.app.Activity;
import android.content.res.Configuration;

public final class HostUi {
    private static final Map<Object, JFrame> windows =
        Collections.synchronizedMap(new WeakHashMap<Object, JFrame>());

    private HostUi() {}

    public static Object createLinearLayout() { return new JPanel(); }
    public static Object createTextView() { return new JLabel(); }
    public static Object createButton() { return new JButton(); }
    public static Object createCheckBox() { return new JCheckBox(); }
    public static Object createImageView() { return new JLabel(); }

    public static void setLinearLayoutOrientation(Object peer, int orientation) {
        JPanel panel = (JPanel) peer;
        panel.setLayout(new BoxLayout(panel,
            orientation == 0 ? BoxLayout.X_AXIS : BoxLayout.Y_AXIS));
    }

    public static void addChild(Object parent, Object child) {
        ((Container) parent).add((Component) child);
    }

    public static void removeAllChildren(Object parent) {
        ((Container) parent).removeAll();
    }

    public static void setText(Object peer, String text) {
        if (peer instanceof JLabel) ((JLabel) peer).setText(text);
        else if (peer instanceof AbstractButton) ((AbstractButton) peer).setText(text);
    }

    public static void setEnabled(Object peer, boolean enabled) {
        ((Component) peer).setEnabled(enabled);
    }

    public static void setButtonIcon(Object peer, String path) {
        AbstractButton button = (AbstractButton) peer;
        if (path == null || path.isEmpty()) {
            button.setIcon(null);
            return;
        }
        ImageIcon source = new ImageIcon(path);
        if (source.getIconWidth() <= 0 || source.getIconHeight() <= 0) {
            button.setIcon(null);
            return;
        }
        Image scaled = source.getImage().getScaledInstance(
            32, 32, Image.SCALE_SMOOTH);
        button.setIcon(new ImageIcon(scaled));
        button.setHorizontalAlignment(AbstractButton.LEFT);
    }

    public static void setImage(Object peer, String path) {
        JLabel label = (JLabel) peer;
        if (path == null || path.isEmpty()) {
            label.setIcon(null);
            return;
        }
        ImageIcon source = new ImageIcon(path);
        label.setIcon(source.getIconWidth() > 0 ? source : null);
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
        JFrame frame = old == null ? new JFrame(title) : old;
        frame.setTitle(title);
        frame.getContentPane().removeAll();
        frame.setDefaultCloseOperation(JFrame.DISPOSE_ON_CLOSE);
        frame.setLayout(new BorderLayout());
        if (content instanceof Component)
            frame.add((Component) content, BorderLayout.CENTER);
        frame.pack();
        frame.setSize(Math.max(frame.getWidth(), 420),
                      Math.max(frame.getHeight(), 280));
        if (old == null) {
            frame.setLocationByPlatform(true);
            frame.addWindowListener(new WindowAdapter() {
                @Override public void windowClosed(WindowEvent event) {
                    windows.remove(activity);
                    if (activity instanceof Activity)
                        ((Activity) activity).dispatchClose();
                }
            });
            frame.addWindowFocusListener(new WindowFocusListener() {
                public void windowGainedFocus(WindowEvent event) {
                    if (activity instanceof Activity)
                        ((Activity) activity).dispatchWindowFocusChanged(true);
                }
                public void windowLostFocus(WindowEvent event) {
                    if (activity instanceof Activity)
                        ((Activity) activity).dispatchWindowFocusChanged(false);
                }
            });
            frame.addComponentListener(new ComponentAdapter() {
                private int orientation;
                @Override public void componentResized(ComponentEvent event) {
                    int next = frame.getWidth() >= frame.getHeight()
                        ? Configuration.ORIENTATION_LANDSCAPE
                        : Configuration.ORIENTATION_PORTRAIT;
                    if (orientation != 0 && orientation != next &&
                        activity instanceof Activity) {
                        Configuration configuration = new Configuration();
                        configuration.orientation = next;
                        ((Activity) activity).getResources().updateConfiguration(
                            configuration,
                            ((Activity) activity).getResources()
                                .getDisplayMetrics());
                        ((Activity) activity).dispatchConfigurationChanged(
                            configuration);
                    }
                    orientation = next;
                }
            });
        }
        windows.put(activity, frame);
        frame.setVisible(true);
        frame.revalidate();
        frame.repaint();
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

    public static void runOnUiThread(Runnable action) {
        if (SwingUtilities.isEventDispatchThread()) action.run();
        else SwingUtilities.invokeLater(action);
    }
}
