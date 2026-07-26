package com.muplar.runtime;

import android.content.Context;
import android.content.ContextWrapper;
import android.content.res.Resources;
import android.util.AttributeSet;
import android.view.InflateException;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.Drawable;

public final class MuplarLayoutInflater extends LayoutInflater {
    private static final String[] FRAMEWORK_PREFIXES = {
        "android.view.",
        "android.widget.",
        "android.webkit.",
    };

    public MuplarLayoutInflater(Context context) {
        super(context);
        installFactory(context);
    }

    private MuplarLayoutInflater(LayoutInflater original, Context context) {
        super(original, context);
        installFactory(context);
    }

    @Override
    public LayoutInflater cloneInContext(Context newContext) {
        return new MuplarLayoutInflater(this, newContext);
    }

    @Override
    protected View onCreateView(String name, AttributeSet attrs)
        throws ClassNotFoundException {
        View bootstrap = createBootstrapView(getContext(), name, attrs);
        if (bootstrap != null) {
            return bootstrap;
        }

        if (name.indexOf('.') >= 0) {
            return createView(name, null, attrs);
        }

        ClassNotFoundException last = null;
        for (String prefix : FRAMEWORK_PREFIXES) {
            try {
                return createView(name, prefix, attrs);
            } catch (ClassNotFoundException e) {
                last = e;
            }
        }

        InflateException failure = new InflateException(
            "Muplar bootstrap inflater cannot create view: " + name);
        if (last != null) {
            failure.initCause(last);
        }
        throw failure;
    }

    @Override
    protected View onCreateView(View parent, String name, AttributeSet attrs)
        throws ClassNotFoundException {
        View bootstrap = createBootstrapView(
            parent != null ? parent.getContext() : getContext(), name, attrs);
        if (bootstrap != null) {
            return bootstrap;
        }
        return super.onCreateView(parent, name, attrs);
    }

    private void installFactory(final Context context) {
        try {
            setFactory2(new Factory2() {
                @Override
                public View onCreateView(View parent,
                                         String name,
                    Context viewContext,
                    AttributeSet attrs) {
                    return createBootstrapView(
                        viewContext != null ? viewContext : context,
                        name,
                        attrs);
                }

                @Override
                public View onCreateView(String name,
                                         Context viewContext,
                                         AttributeSet attrs) {
                    return createBootstrapView(
                        viewContext != null ? viewContext : context,
                        name,
                        attrs);
                }
            });
        } catch (IllegalStateException ignored) {
        }
    }

    private View createBootstrapView(Context context,
                                     String name,
                                     AttributeSet attrs) {
        if ("fragment".equals(name) || "android.app.Fragment".equals(name)) {
            android.widget.FrameLayout placeholder =
                new android.widget.FrameLayout(createFrameworkThemeContext(context));
            copyAndroidId(placeholder, attrs);
            return placeholder;
        }

        if ("Button".equals(name) || "android.widget.Button".equals(name)) {
            android.widget.Button button =
                new android.widget.Button(createFrameworkThemeContext(context));
            copyAndroidId(button, attrs);
            return button;
        }

        if ("com.android.launcher3.allapps.search.AppsSearchContainerLayout"
                .equals(name)) {
            try {
                Context safeContext = createDrawableFallbackContext(context);
                Class<?> type = Class.forName(name, false, context.getClassLoader());
                java.lang.reflect.Constructor<?> ctor =
                    type.getConstructor(
                        Context.class, AttributeSet.class, Integer.TYPE);
                View view = (View) ctor.newInstance(
                    safeContext, null, Integer.valueOf(0));
                copyAndroidId(view, attrs);
                return view;
            } catch (Throwable t) {
                logInflateFailure(name, t);
                throw new InflateException(
                    "Muplar bootstrap inflater cannot create view: " + name, t);
            }
        }

        if (!"com.android.launcher3.DeleteDropTarget".equals(name) &&
            !"com.android.launcher3.SecondaryDropTarget".equals(name) &&
            !"com.android.launcher3.ButtonDropTarget".equals(name) &&
            !"com.android.quickstep.views.ClearAllButton".equals(name) &&
            !"com.android.quickstep.views.TaskView".equals(name) &&
            !"com.android.quickstep.views.GroupedTaskView".equals(name) &&
            !"com.android.quickstep.views.DesktopTaskView".equals(name)) {
            return null;
        }
        try {
            Context safeContext = createFrameworkThemeContext(context);
            Class<?> type = Class.forName(name, false, context.getClassLoader());
            java.lang.reflect.Constructor<?> ctor =
                type.getConstructor(Context.class, AttributeSet.class);
            View view = (View) ctor.newInstance(safeContext, null);
            copyAndroidId(view, attrs);
            return view;
        } catch (Throwable t) {
            logInflateFailure(name, t);
            throw new InflateException(
                "Muplar bootstrap inflater cannot create view: " + name, t);
        }
    }

    private static void logInflateFailure(String name, Throwable t) {
        System.err.println("[Muplar/ART] failed to create view " + name);
        Throwable cursor = t;
        int depth = 0;
        while (cursor != null && depth < 8) {
            System.err.println("[Muplar/ART]   cause[" + depth + "] "
                + cursor.getClass().getName() + ": " + cursor.getMessage());
            StackTraceElement[] stack = cursor.getStackTrace();
            int count = Math.min(stack.length, 12);
            for (int i = 0; i < count; i++) {
                System.err.println("[Muplar/ART]     at " + stack[i]);
            }
            cursor = cursor.getCause();
            depth++;
        }
    }

    private Context createFrameworkThemeContext(Context context) {
        return new FrameworkThemeContext(context);
    }

    private Context createDrawableFallbackContext(Context context) {
        return new DrawableFallbackContext(context);
    }

    private static void copyAndroidId(View view, AttributeSet attrs) {
        if (attrs == null) {
            return;
        }
        int id = attrs.getAttributeResourceValue(
            "http://schemas.android.com/apk/res/android", "id", View.NO_ID);
        if (id != View.NO_ID) {
            view.setId(id);
        }
    }

    private static final class FrameworkThemeContext extends ContextWrapper {
        private final Resources.Theme theme;

        FrameworkThemeContext(Context base) {
            super(base);
            theme = base.getResources().newTheme();
            boolean copied = false;
            try {
                theme.setTo(base.getTheme());
                copied = true;
            } catch (Throwable ignored) {
            }
            if (!copied) {
                applyFrameworkBaseTheme(theme);
            }
        }

        @Override
        public Resources.Theme getTheme() {
            return theme;
        }

        @Override
        public void setTheme(int resid) {
            theme.applyStyle(resid, true);
        }
    }

    private static final class DrawableFallbackContext extends ContextWrapper {
        private final Resources resources;

        DrawableFallbackContext(Context base) {
            super(base);
            resources = new DrawableFallbackResources(base.getResources());
        }

        @Override
        public Resources getResources() {
            return resources;
        }
    }

    private static final class DrawableFallbackResources extends Resources {
        DrawableFallbackResources(Resources base) {
            super(base.getAssets(),
                  base.getDisplayMetrics(),
                  base.getConfiguration());
        }

        @Override
        public Drawable getDrawable(int id) throws NotFoundException {
            try {
                return super.getDrawable(id);
            } catch (Throwable t) {
                return fallback(id, t);
            }
        }

        @Override
        public Drawable getDrawable(int id, Theme theme) throws NotFoundException {
            try {
                return super.getDrawable(id, theme);
            } catch (Throwable t) {
                return fallback(id, t);
            }
        }

        private Drawable fallback(int id, Throwable t) {
            System.err.println("[Muplar/ART] drawable fallback for resource 0x"
                + Integer.toHexString(id) + ": " + t);
            return new ColorDrawable(0x00000000);
        }
    }

    private static void applyFrameworkBaseTheme(Resources.Theme theme) {
        try {
            Class<?> styles = Class.forName("android.R$style");
            int style = styles.getField("Theme_Material_Light").getInt(null);
            theme.applyStyle(style, true);
        } catch (Throwable ignored) {
        }
    }
}
