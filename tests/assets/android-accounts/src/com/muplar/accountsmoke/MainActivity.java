package com.muplar.accountsmoke;

import android.accounts.Account;
import android.accounts.AccountManager;
import android.accounts.AccountManagerFuture;
import android.accounts.AuthenticatorException;
import android.accounts.OnAccountsUpdateListener;
import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;

public final class MainActivity extends Activity {
    private int callbacks;
    private boolean passed;
    private AccountManager accounts;
    private OnAccountsUpdateListener listener;

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        checkStableDisplaySize();
        accounts = AccountManager.get(this);
        require(accounts != null, "AccountManager missing");
        require(accounts == getSystemService(AccountManager.class), "typed service mismatch");
        require(accounts.getAccounts().length == 0, "unexpected accounts");
        require(accounts.getAccountsByType("com.google").length == 0, "unexpected Google accounts");
        require(accounts.getAuthenticatorTypes().length == 0, "unexpected authenticator");
        Account absent = new Account("absent", "com.muplar.accountsmoke");
        require(!accounts.addAccountExplicitly(absent, null, null), "unsupported account creation succeeded");
        require(accounts.peekAuthToken(absent, "test") == null, "invented auth token");
        Handler handler = new Handler(getMainLooper());
        listener = values -> {
            require(values != null && values.length == 0, "invalid listener snapshot");
            complete(1);
        };
        accounts.addOnAccountsUpdatedListener(listener, handler, true);
        accounts.getAccountsByTypeAndFeatures("com.google", new String[] {"test"}, future -> {
            try {
                require(future.getResult().length == 0, "feature query must be empty");
                complete(2);
            } catch (Exception error) { throw new AssertionError(error); }
        }, handler);
        accounts.hasFeatures(absent, new String[] {"test"}, future -> {
            try {
                require(!future.getResult(), "absent account has features");
                complete(4);
            } catch (Exception error) { throw new AssertionError(error); }
        }, handler);
        accounts.addAccount("com.google", null, null, null, this, future -> {
            assertUnsupported(future);
            complete(8);
        }, handler);
        accounts.getAuthToken(absent, "test", new Bundle(), false, future -> {
            assertUnsupported(future);
            complete(16);
        }, handler);
        handler.postDelayed(() -> require(passed, "account callbacks did not complete"), 5000);
    }

    private void checkStableDisplaySize() {
        try {
            android.hardware.display.DisplayManager manager =
                getSystemService(android.hardware.display.DisplayManager.class);
            java.lang.reflect.Method getter = manager.getClass().getMethod("getStableDisplaySize");
            android.graphics.Point stable = (android.graphics.Point) getter.invoke(manager);
            android.graphics.Point actual = new android.graphics.Point();
            manager.getDisplay(android.view.Display.DEFAULT_DISPLAY).getRealSize(actual);
            require(stable != null && stable.x > 0 && stable.y > 0, "stable display size missing");
            require(stable.equals(actual), "stable display size differs from actual display");
            stable.x = 0;
            require(actual.equals(getter.invoke(manager)), "caller mutated shared display size");
        } catch (ReflectiveOperationException error) {
            throw new AssertionError(error);
        }
    }

    private void complete(int callback) {
        callbacks |= callback;
        if (!passed && callbacks == 31) {
            accounts.removeOnAccountsUpdatedListener(listener);
            require(accounts.getAccounts().length == 0, "failed mutation changed accounts");
            passed = true;
            System.out.println("ACCOUNT_SERVICE_SMOKE_PASS queries=empty callbacks=5 auth=unsupported display=stable");
        }
    }

    private static void assertUnsupported(AccountManagerFuture<Bundle> future) {
        require(future.isDone() && !future.isCancelled(), "authentication future incomplete");
        try {
            future.getResult();
            throw new AssertionError("authentication unexpectedly succeeded");
        } catch (UnsupportedOperationException expected) {
        } catch (AuthenticatorException error) {
            require(error.getCause() instanceof UnsupportedOperationException,
                "unexpected authenticator error: " + error);
        } catch (Exception error) {
            throw new AssertionError(error);
        }
    }

    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
}
