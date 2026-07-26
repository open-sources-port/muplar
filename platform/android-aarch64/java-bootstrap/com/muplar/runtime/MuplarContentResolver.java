package com.muplar.runtime;

import android.content.ContentResolver;
import android.content.Context;
import android.content.IContentProvider;
import java.lang.reflect.Array;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;

public final class MuplarContentResolver extends ContentResolver {
    private final IContentProvider provider;

    public MuplarContentResolver(Context context) {
        super(context);
        this.provider = createProvider();
    }

    protected IContentProvider acquireProvider(Context context, String name) {
        return provider;
    }

    protected IContentProvider acquireUnstableProvider(Context context, String name) {
        return provider;
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

    private static IContentProvider createProvider() {
        try {
            Class<?> type = Class.forName("android.content.IContentProvider");
            return (IContentProvider)Proxy.newProxyInstance(
                type.getClassLoader(),
                new Class<?>[] { type },
                new ProviderHandler());
        } catch (Throwable ignored) {
            return null;
        }
    }

    private static final class ProviderHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args)
            throws Throwable {
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
    }
}
