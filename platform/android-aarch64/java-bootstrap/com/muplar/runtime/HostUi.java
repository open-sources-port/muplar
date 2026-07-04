package com.muplar.runtime;

import java.awt.BorderLayout;
import java.awt.Component;
import java.awt.Container;
import java.awt.Frame;
import java.awt.Image;
import java.awt.AWTEvent;
import java.awt.Point;
import java.awt.Toolkit;
import java.awt.Graphics2D;
import java.awt.image.BufferedImage;
import java.io.File;
import java.awt.event.ComponentAdapter;
import java.awt.event.ComponentEvent;
import java.awt.event.WindowAdapter;
import java.awt.event.WindowEvent;
import java.awt.event.WindowFocusListener;
import java.awt.event.MouseAdapter;
import java.awt.event.MouseEvent;
import java.awt.event.AWTEventListener;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
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
import javax.imageio.ImageIO;
import android.app.Activity;
import android.content.res.Configuration;
import android.view.View;
import android.view.ViewGroup;
import android.view.MotionEvent;

public final class HostUi {
    private static final Map<Object, JFrame> windows =
        Collections.synchronizedMap(new WeakHashMap<Object, JFrame>());
    private static volatile View testLaunchTarget;

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
        runOnUiThread(() -> {
            Container container = (Container) parent;
            Component component = (Component) child;
            if (component.getParent() != null) return;
            container.add(component);
        });
    }

    public static void removeAllChildren(Object parent) {
        runOnUiThread(() -> {
            Container container = (Container) parent;
            for (Component child : container.getComponents()) child.setVisible(false);
        });
    }

    public static void setText(Object peer, String text) {
        if (peer instanceof JLabel) ((JLabel) peer).setText(text);
        else if (peer instanceof AbstractButton) ((AbstractButton) peer).setText(text);
    }

    public static void setEnabled(Object peer, boolean enabled) {
        ((Component) peer).setEnabled(enabled);
    }

    public static void setVisibility(Object peer, boolean visible) {
        Component component = (Component) peer;
        component.setVisible(visible);
        Container parent = component.getParent();
        if (parent != null) {
            parent.revalidate();
            parent.repaint();
        }
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

    public static void setOnViewClickListener(Object peer, final Runnable listener) {
        Component component = (Component) peer;
        Object old = component instanceof javax.swing.JComponent
            ? ((javax.swing.JComponent) component).getClientProperty("muplar.click")
            : null;
        if (old instanceof MouseAdapter) component.removeMouseListener((MouseAdapter) old);
        if (listener == null) return;
        MouseAdapter adapter = new MouseAdapter() {
            @Override public void mouseClicked(MouseEvent event) { listener.run(); }
        };
        component.addMouseListener(adapter);
        if (component instanceof javax.swing.JComponent)
            ((javax.swing.JComponent) component).putClientProperty("muplar.click", adapter);
    }

    public static int materializeAdapters(View root) {
        if (root == null) return 0;
        int count = materializeAdapter(root);
        if (root instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) root;
            int children = group.getChildCount();
            for (int i = 0; i < children; ++i)
                count += materializeAdapters(group.getChildAt(i));
        }
        return count;
    }

    private static int materializeAdapter(View view) {
        if (!hasSuperclass(view.getClass(), "androidx.recyclerview.widget.RecyclerView") ||
                !(view instanceof ViewGroup) || ((ViewGroup) view).getChildCount() != 0)
            return 0;
        try {
            Method getAdapter = view.getClass().getMethod("getAdapter");
            Object adapter = getAdapter.invoke(view);
            if (adapter == null) {
                System.out.println("[HostUi] RecyclerView has no adapter: " +
                    view.getClass().getName());
                return 0;
            }
            Method getItemCount = adapter.getClass().getMethod("getItemCount");
            Method getItemViewType = adapter.getClass().getMethod(
                "getItemViewType", int.class);
            Method create = findPublicMethod(adapter.getClass(), "createViewHolder", 2);
            Method bind = findPublicMethod(adapter.getClass(), "bindViewHolder", 2);
            refreshAdapterModel(view);
            int itemCount = ((Number) getItemCount.invoke(adapter)).intValue();
            System.out.println("[HostUi] RecyclerView " + view.getClass().getName() +
                " adapter=" + adapter.getClass().getName() + " items=" + itemCount);
            if (itemCount == 0) logAppsState(view);
            int added = 0;
            for (int position = 0; position < itemCount; ++position) {
                int type = ((Number) getItemViewType.invoke(adapter, position)).intValue();
                Object holder = create.invoke(adapter, view, type);
                bind.invoke(adapter, holder, position);
                Field itemView = holder.getClass().getField("itemView");
                Object item = itemView.get(holder);
                if (item instanceof View) {
                    View boundView = (View) item;
                    ((ViewGroup) view).addView(boundView);
                    if (testLaunchTarget == null && boundView.isClickable())
                        testLaunchTarget = boundView;
                    ++added;
                }
            }
            if (added > 0)
                System.out.println("[HostUi] materialized RecyclerView items=" + added);
            return added;
        } catch (ReflectiveOperationException error) {
            Throwable cause = error instanceof java.lang.reflect.InvocationTargetException
                ? ((java.lang.reflect.InvocationTargetException) error).getCause() : error;
            System.err.println("[HostUi] RecyclerView materialization failed: " + cause);
            if (cause != null) cause.printStackTrace(System.err);
            return 0;
        }
    }

    private static void refreshAdapterModel(View view) {
        try {
            Method getApps = view.getClass().getMethod("getApps");
            Object apps = getApps.invoke(view);
            if (apps != null) apps.getClass().getMethod("onAppsUpdated").invoke(apps);
        } catch (ReflectiveOperationException ignored) {}
    }

    private static void logAppsState(View view) {
        try {
            Method getApps = view.getClass().getMethod("getApps");
            Object apps = getApps.invoke(view);
            if (apps == null) return;
            Method filtered = apps.getClass().getMethod("getNumFilteredApps");
            Method items = apps.getClass().getMethod("getAdapterItems");
            Object adapterItems = items.invoke(apps);
            int adapterCount = adapterItems instanceof java.util.Collection
                ? ((java.util.Collection<?>) adapterItems).size() : -1;
            System.out.println("[HostUi] apps filtered=" + filtered.invoke(apps) +
                " adapterItems=" + adapterCount);
        } catch (ReflectiveOperationException ignored) {}
    }

    private static boolean hasSuperclass(Class<?> type, String name) {
        for (Class<?> current = type; current != null; current = current.getSuperclass())
            if (name.equals(current.getName())) return true;
        return false;
    }

    private static Method findPublicMethod(Class<?> type, String name, int parameters)
            throws NoSuchMethodException {
        for (Method method : type.getMethods())
            if (name.equals(method.getName()) &&
                    method.getParameterTypes().length == parameters)
                return method;
        throw new NoSuchMethodException(type.getName() + "." + name);
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
        final View contentView = content instanceof View ? (View) content : null;
        final Object contentPeer = contentView == null ? content : contentView.getPeer();
        JFrame old = windows.remove(activity);
        JFrame frame = old == null ? new JFrame(title) : old;
        frame.setTitle(title);
        frame.getContentPane().removeAll();
        frame.setDefaultCloseOperation(JFrame.DISPOSE_ON_CLOSE);
        frame.setLayout(new BorderLayout());
        if (contentPeer instanceof Component)
            frame.add((Component) contentPeer, BorderLayout.CENTER);

        JPanel navBar = new JPanel();
        navBar.setLayout(new java.awt.FlowLayout(java.awt.FlowLayout.CENTER, 40, 5));
        navBar.setBackground(java.awt.Color.BLACK);

        JButton backBtn = new JButton("◀");
        JButton homeBtn = new JButton("●");
        JButton drawerBtn = new JButton("㗊");

        java.awt.Font buttonFont = new java.awt.Font("SansSerif", java.awt.Font.BOLD, 18);
        java.awt.Color defaultColor = java.awt.Color.WHITE;
        java.awt.Color hoverColor = new java.awt.Color(0, 191, 255); // Deep Sky Blue glow
        java.awt.Color hoverBg = new java.awt.Color(50, 50, 50); // Dark grey highlight capsule

        for (JButton btn : new JButton[]{backBtn, homeBtn, drawerBtn}) {
            btn.setFont(buttonFont);
            btn.setForeground(defaultColor);
            btn.setContentAreaFilled(false);
            btn.setBorderPainted(false);
            btn.setFocusPainted(false);
            btn.setOpaque(false);
            btn.setMargin(new java.awt.Insets(5, 20, 5, 20));
            btn.setCursor(java.awt.Cursor.getPredefinedCursor(java.awt.Cursor.HAND_CURSOR));
            btn.addMouseListener(new java.awt.event.MouseAdapter() {
                @Override public void mouseEntered(java.awt.event.MouseEvent e) {
                    btn.setForeground(hoverColor);
                    btn.setBackground(hoverBg);
                    btn.setContentAreaFilled(true);
                    btn.setOpaque(true);
                }
                @Override public void mouseExited(java.awt.event.MouseEvent e) {
                    btn.setForeground(defaultColor);
                    btn.setContentAreaFilled(false);
                    btn.setOpaque(false);
                }
            });
            navBar.add(btn);
        }

        backBtn.addActionListener(e -> {
            if (activity instanceof Activity) {
                ((Activity) activity).onBackPressed();
            }
        });

        homeBtn.addActionListener(e -> {
            if (activity instanceof Activity) {
                Activity act = (Activity) activity;
                if (!"com.android.launcher3".equals(act.getPackageName())) {
                    act.finish();
                } else {
                    act.onBackPressed();
                }
            }
        });

        drawerBtn.addActionListener(e -> {
            if (activity instanceof Activity) {
                Activity act = (Activity) activity;
                if (!"com.android.launcher3".equals(act.getPackageName())) {
                    act.finish();
                } else {
                    if (contentView != null) {
                        long down = System.currentTimeMillis();
                        float x = contentView.getWidth() / 2.0f;
                        float bottom = contentView.getHeight() * 0.85f;
                        float top = contentView.getHeight() * 0.20f;
                        contentView.dispatchTouchEvent(MotionEvent.obtain(
                            down, down, MotionEvent.ACTION_DOWN, x, bottom, 0));
                        for (int step = 1; step <= 5; step++) {
                            float y = bottom + (top - bottom) * step / 5.0f;
                            contentView.dispatchTouchEvent(MotionEvent.obtain(
                                down, down + step * 16L, MotionEvent.ACTION_MOVE, x, y, 0));
                        }
                        contentView.dispatchTouchEvent(MotionEvent.obtain(
                            down, down + 100L, MotionEvent.ACTION_UP, x, top, 0));
                    }
                }
            }
        });

        frame.add(navBar, BorderLayout.SOUTH);
        frame.pack();

        int targetWidth = 1242;
        int targetHeight = 2688;
        double scale = 1.0;
        try {
            int screenHeight = Toolkit.getDefaultToolkit().getScreenSize().height;
            int maxAllowedHeight = Math.min(850, screenHeight - 150);
            if (targetHeight > maxAllowedHeight) {
                scale = (double) maxAllowedHeight / targetHeight;
            }
        } catch (Exception ignored) {
            scale = 0.333;
        }
        int frameWidth = (int) (targetWidth * scale);
        int frameHeight = (int) (targetHeight * scale) + 35;
        frame.setSize(frameWidth, frameHeight);
        if (old == null) {
            frame.setLocationByPlatform(true);
            frame.addWindowListener(new WindowAdapter() {
                @Override public void windowClosed(WindowEvent event) {
                    windows.remove(activity);
                    Object bridge = frame.getRootPane().getClientProperty(
                        "muplar.pointerBridge");
                    if (bridge instanceof AWTEventListener)
                        Toolkit.getDefaultToolkit().removeAWTEventListener(
                            (AWTEventListener) bridge);
                    Object pulse = frame.getRootPane().getClientProperty(
                        "muplar.framePulse");
                    if (pulse instanceof javax.swing.Timer)
                        ((javax.swing.Timer) pulse).stop();
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
        if (contentView != null) {
            installPointerBridge(frame, contentView,
                activity instanceof Activity ? (Activity) activity : null);
            installFramePulse(frame, contentView);
        }
        frame.setVisible(true);
        frame.revalidate();
        frame.repaint();
        frame.toFront();
        frame.requestFocus();
    }

    private static void installFramePulse(JFrame frame, View root) {
        Object previous = frame.getRootPane().getClientProperty("muplar.framePulse");
        if (previous instanceof javax.swing.Timer) ((javax.swing.Timer) previous).stop();
        javax.swing.Timer pulse = new javax.swing.Timer(16, event -> {
            if (!frame.isDisplayable()) {
                ((javax.swing.Timer) event.getSource()).stop();
                return;
            }
            dispatchFrame(root);
        });
        pulse.setCoalesce(true);
        frame.getRootPane().putClientProperty("muplar.framePulse", pulse);
        pulse.start();
    }

    private static void dispatchFrame(View view) {
        view.getViewTreeObserver().dispatchOnGlobalLayout();
        if (view.getViewTreeObserver().dispatchOnPreDraw())
            view.getViewTreeObserver().dispatchOnDraw();
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); i++)
                dispatchFrame(group.getChildAt(i));
        }
    }

    private static void installPointerBridge(JFrame frame, View root,
            Activity activity) {
        Object previous = frame.getRootPane().getClientProperty("muplar.pointerBridge");
        if (previous instanceof AWTEventListener)
            Toolkit.getDefaultToolkit().removeAWTEventListener((AWTEventListener) previous);
        AWTEventListener listener = new AWTEventListener() {
            private long downTime;
            @Override public void eventDispatched(AWTEvent raw) {
                if (!(raw instanceof MouseEvent)) return;
                MouseEvent event = (MouseEvent) raw;
                if (!(event.getSource() instanceof Component) ||
                        SwingUtilities.getWindowAncestor((Component) event.getSource()) != frame)
                    return;
                int action;
                if (event.getID() == MouseEvent.MOUSE_PRESSED) {
                    action = MotionEvent.ACTION_DOWN;
                    downTime = event.getWhen();
                } else if (event.getID() == MouseEvent.MOUSE_DRAGGED) {
                    action = MotionEvent.ACTION_MOVE;
                } else if (event.getID() == MouseEvent.MOUSE_RELEASED) {
                    action = MotionEvent.ACTION_UP;
                } else return;
                Point point = SwingUtilities.convertPoint(
                    (Component) event.getSource(), event.getPoint(), frame.getContentPane());
                float androidX = point.x * root.getWidth() /
                    (float) Math.max(1, frame.getContentPane().getWidth());
                float androidY = point.y * root.getHeight() /
                    (float) Math.max(1, frame.getContentPane().getHeight());
                MotionEvent motion = MotionEvent.obtain(downTime, event.getWhen(), action,
                    androidX, androidY, event.getModifiersEx());
                if (activity != null) activity.dispatchTouchEvent(motion);
                else root.dispatchTouchEvent(motion);
            }
        };
        frame.getRootPane().putClientProperty("muplar.pointerBridge", listener);
        Toolkit.getDefaultToolkit().addAWTEventListener(listener,
            AWTEvent.MOUSE_EVENT_MASK | AWTEvent.MOUSE_MOTION_EVENT_MASK);
        if ("1".equals(System.getenv("MUPLAR_HOST_TEST_SWIPE_UP"))) {
            javax.swing.Timer timer = new javax.swing.Timer(4500, event -> {
                long down = System.currentTimeMillis();
                float x = root.getWidth() / 2.0f;
                float bottom = root.getHeight() * 0.85f;
                float top = root.getHeight() * 0.20f;
                root.dispatchTouchEvent(MotionEvent.obtain(
                    down, down, MotionEvent.ACTION_DOWN, x, bottom, 0));
                for (int step = 1; step <= 5; step++) {
                    float y = bottom + (top - bottom) * step / 5.0f;
                    root.dispatchTouchEvent(MotionEvent.obtain(
                        down, down + step * 16L, MotionEvent.ACTION_MOVE, x, y, 0));
                }
                root.dispatchTouchEvent(MotionEvent.obtain(
                    down, down + 100L, MotionEvent.ACTION_UP, x, top, 0));
                System.out.println("[HostUi] test swipe-up dispatched");
                String screenshot = System.getenv("MUPLAR_HOST_TEST_SCREENSHOT");
                if (screenshot != null && !screenshot.isEmpty()) {
                    javax.swing.Timer captureTimer = new javax.swing.Timer(500,
                        ignored -> captureFrame(frame, screenshot));
                    captureTimer.setRepeats(false);
                    captureTimer.start();
                }
                if ("1".equals(System.getenv("MUPLAR_HOST_TEST_LONG_CLICK_FIRST_APP"))) {
                    javax.swing.Timer longClickTimer = new javax.swing.Timer(1000,
                        ignored -> {
                            View target = testLaunchTarget;
                            long pressTime = System.currentTimeMillis();
                            float pressX = root.getWidth() / 2.0f;
                            float pressY = root.getHeight() * 0.75f;
                            MotionEvent press = MotionEvent.obtain(pressTime, pressTime,
                                MotionEvent.ACTION_DOWN, pressX, pressY, 0);
                            if (activity != null) activity.dispatchTouchEvent(press);
                            else root.dispatchTouchEvent(press);
                            boolean clicked = target != null && target.performLongClick();
                            boolean dragging = isLauncherDragging(target);
                            System.out.println("[HostUi] test first app long-click dispatched=" +
                                clicked + " target=" + (target == null ? "null" :
                                target.getClass().getName()) + " dragging=" + dragging +
                                " guards=" + launcherDragGuards(target));
                            if (dragging) {
                                float dragX = root.getWidth() / 2.0f;
                                float startY = pressY;
                                float endY = root.getHeight() * 0.20f;
                                for (int step = 1; step <= 8; step++) {
                                    float dragY = startY + (endY - startY) * step / 8.0f;
                                    MotionEvent move = MotionEvent.obtain(pressTime,
                                        pressTime + step * 32L, MotionEvent.ACTION_MOVE,
                                        dragX, dragY, 0);
                                    if (activity != null) activity.dispatchTouchEvent(move);
                                    else root.dispatchTouchEvent(move);
                                }
                                MotionEvent release = MotionEvent.obtain(pressTime,
                                    pressTime + 300L, MotionEvent.ACTION_UP,
                                    dragX, endY, 0);
                                if (activity != null) activity.dispatchTouchEvent(release);
                                else root.dispatchTouchEvent(release);
                                javax.swing.Timer stateTimer = new javax.swing.Timer(500,
                                    later -> System.out.println(
                                        "[HostUi] test app drop completed dragging=" +
                                        isLauncherDragging(target)));
                                stateTimer.setRepeats(false);
                                stateTimer.start();
                            }
                        });
                    longClickTimer.setRepeats(false);
                    longClickTimer.start();
                } else if ("1".equals(System.getenv("MUPLAR_HOST_TEST_CLICK_FIRST_APP"))) {
                    javax.swing.Timer clickTimer = new javax.swing.Timer(1000, ignored -> {
                        View target = testLaunchTarget;
                        boolean clicked = target != null && target.performClick();
                        System.out.println("[HostUi] test first app click dispatched=" +
                            clicked);
                    });
                    clickTimer.setRepeats(false);
                    clickTimer.start();
                }
            });
            timer.setRepeats(false);
            timer.start();
        }
    }

    private static void captureFrame(JFrame frame, String path) {
        try {
            int width = Math.max(1, frame.getContentPane().getWidth());
            int height = Math.max(1, frame.getContentPane().getHeight());
            BufferedImage image = new BufferedImage(width, height,
                BufferedImage.TYPE_INT_ARGB);
            Graphics2D graphics = image.createGraphics();
            frame.getContentPane().printAll(graphics);
            graphics.dispose();
            File output = new File(path);
            File parent = output.getParentFile();
            if (parent != null && !parent.isDirectory() && !parent.mkdirs())
                throw new IllegalStateException("cannot create " + parent);
            ImageIO.write(image, "png", output);
            System.out.println("[HostUi] screenshot captured=" + output.getAbsolutePath() +
                " size=" + width + "x" + height);
        } catch (Exception error) {
            System.err.println("[HostUi] screenshot failed: " + error);
        }
    }

    private static boolean isLauncherDragging(View target) {
        if (target == null || target.getContext() == null) return false;
        try {
            Object launcher = target.getContext();
            Method getController = launcher.getClass().getMethod("getDragController");
            Object controller = getController.invoke(launcher);
            return Boolean.TRUE.equals(controller.getClass().getMethod("isDragging")
                .invoke(controller));
        } catch (ReflectiveOperationException ignored) {
            return false;
        }
    }

    private static String launcherDragGuards(View target) {
        if (target == null || target.getContext() == null) return "no-context";
        try {
            Object launcher = target.getContext();
            boolean locked = Boolean.TRUE.equals(launcher.getClass()
                .getMethod("isWorkspaceLocked").invoke(launcher));
            Object workspace = launcher.getClass().getMethod("getWorkspace")
                .invoke(launcher);
            boolean switching = Boolean.TRUE.equals(workspace.getClass()
                .getMethod("isSwitchingState").invoke(workspace));
            Object stateManager = launcher.getClass().getMethod("getStateManager")
                .invoke(launcher);
            Object state = stateManager.getClass().getMethod("getState")
                .invoke(stateManager);
            return "locked=" + locked + ",switching=" + switching +
                ",state=" + state;
        } catch (ReflectiveOperationException error) {
            return "unavailable:" + error.getClass().getSimpleName();
        }
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
