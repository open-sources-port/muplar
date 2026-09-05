package com.muplar.runtime;

import android.content.ContentResolver;
import android.content.Context;
import android.content.IContentProvider;
import android.os.Bundle;
import java.lang.reflect.Array;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;

public final class MuplarContentResolver extends ContentResolver {
    private IContentProvider provider;

    public MuplarContentResolver(Context context) {
        super(context);
    }

    private synchronized IContentProvider getProvider() {
        if (provider == null) {
            provider = createProvider();
        }
        return provider;
    }

    protected IContentProvider acquireProvider(Context context, String name) {
        return getProvider();
    }

    protected IContentProvider acquireExistingProvider(Context context, String name) {
        return getProvider();
    }

    protected IContentProvider acquireUnstableProvider(Context context, String name) {
        return getProvider();
    }

    public boolean releaseProvider(IContentProvider provider) {
        return true;
    }

    public boolean releaseUnstableProvider(IContentProvider provider) {
        return true;
    }

    public void unstableProviderDied(IContentProvider provider) {
    }

    public void appNotRespondingViaProvider(IContentProvider provider) {
    }

    private static ClassLoader getSafeClassLoader(Class<?> type) {
        if (type != null && type.getClassLoader() != null) {
            return type.getClassLoader();
        }
        if (MuplarContentResolver.class.getClassLoader() != null) {
            return MuplarContentResolver.class.getClassLoader();
        }
        ClassLoader cl = Thread.currentThread().getContextClassLoader();
        if (cl != null) {
            return cl;
        }
        return ClassLoader.getSystemClassLoader();
    }

    private static IContentProvider createProvider() {
        try {
            Class<?> type = Class.forName("android.content.IContentProvider");
            return (IContentProvider)Proxy.newProxyInstance(
                getSafeClassLoader(type),
                new Class<?>[] { type },
                new ProviderHandler());
        } catch (Throwable t) {
            System.err.println("[Muplar/ART] createProvider failed: " + t);
            return null;
        }
    }

    private static final class ProviderHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args)
            throws Throwable {
            String methodName = method.getName();
            if ("toString".equals(methodName)) {
                return "MuplarContentProviderProxy";
            }
            if ("asBinder".equals(methodName)) {
                return new android.os.Binder();
            }
            if ("call".equals(methodName)) {
                Object result = handleCall(args);
                if (result != null) {
                    return result;
                }
            }
            if ("query".equals(methodName)) {
                Object result = handleQuery(args);
                if (result != null) {
                    return result;
                }
            }

            Class<?> returnType = method.getReturnType();
            if (returnType == Void.TYPE) {
                return null;
            }
            if (returnType == Boolean.TYPE) {
                return Boolean.FALSE;
            }
            if (returnType == Integer.TYPE) {
                return Integer.valueOf(0);
            }
            if (returnType == Long.TYPE) {
                return Long.valueOf(0);
            }
            if (returnType.isArray()) {
                return Array.newInstance(returnType.getComponentType(), 0);
            }
            return null;
        }

        private Object handleCall(Object[] args) {
            if (args == null || args.length == 0) {
                return null;
            }
            String key = null;
            for (Object arg : args) {
                if (arg instanceof String) {
                    String str = (String) arg;
                    if (str.equals("settings") ||
                        str.startsWith("GET_") || str.startsWith("PUT_") ||
                        str.contains("://") || str.equals("system") ||
                        str.equals("secure") || str.equals("global") ||
                        str.equals("config") || str.contains("com.android")) {
                        continue;
                    }
                    key = str;
                    break;
                }
            }
            if (key == null) {
                return null;
            }
            String value = getSettingValue(key);
            if (value == null) {
                return null;
            }
            Bundle bundle = new Bundle();
            bundle.putString("value", value);
            return bundle;
        }

        private Object handleQuery(Object[] args) {
            String key = null;
            if (args != null) {
                for (Object arg : args) {
                    if (arg instanceof Bundle) {
                        Bundle b = (Bundle) arg;
                        String[] selArgs = b.getStringArray("android:query-arg-sql-selection-args");
                        if (selArgs != null && selArgs.length > 0) {
                            key = selArgs[0];
                            break;
                        }
                    } else if (arg instanceof String[]) {
                        String[] strArr = (String[]) arg;
                        if (strArr.length == 1 && strArr[0] != null) {
                            key = strArr[0];
                        }
                    }
                }
            }
            if (key != null) {
                String val = getSettingValue(key);
                if (val != null) {
                    android.database.MatrixCursor c = new android.database.MatrixCursor(new String[]{"_id", "name", "value"});
                    c.addRow(new Object[]{1L, key, val});
                    return c;
                }
            }
            return null;
        }

        private String getSettingValue(String key) {
            if ("user_setup_complete".equals(key) ||
                "accelerometer_rotation".equals(key) ||
                "touchpad_natural_scrolling".equals(key) ||
                "notification_badging".equals(key) ||
                "render_shadows_in_compositor".equals(key) ||
                "launcher_broadcast_installed_apps".equals(key)) {
                return "1";
            }
            if ("animator_duration_scale".equals(key) ||
                "transition_animation_scale".equals(key) ||
                "window_animation_scale".equals(key)) {
                return "1.0";
            }
            if ("nav_bar_kids_mode".equals(key) ||
                "swipe_bottom_to_notification_enabled".equals(key) ||
                "override_desktop_mode_features".equals(key)) {
                return "0";
            }
            if ("launcher3.layout.provider.blob".equals(key) ||
                "launcher3.layout.provider".equals(key)) {
                return "";
            }
            return null;
        }
    }
}
