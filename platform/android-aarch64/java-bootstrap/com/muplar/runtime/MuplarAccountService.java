package com.muplar.runtime;

import android.accounts.Account;
import android.accounts.AccountManager;
import android.accounts.AuthenticatorDescription;
import android.os.Bundle;
import java.lang.reflect.Method;
import java.util.Collections;

/** Signed-out account service. Authenticator execution and account storage are not implemented. */
final class MuplarAccountService {
    private MuplarAccountService() {}

    static Object invoke(Method method, Object[] args) throws Exception {
        String name = method.getName();
        if ("getAccounts".equals(name) || "getAccountsAsUser".equals(name) ||
            "getAccountsForPackage".equals(name) || "getAccountsByTypeForPackage".equals(name) ||
            "getSharedAccountsAsUser".equals(name)) {
            return new Account[0];
        }
        if ("getAuthenticatorTypes".equals(name)) {
            return new AuthenticatorDescription[0];
        }
        if ("getPackagesAndVisibilityForAccount".equals(name) ||
            "getAccountsAndVisibilityForPackage".equals(name)) {
            return Collections.emptyMap();
        }
        if ("getPassword".equals(name) || "getUserData".equals(name) ||
            "peekAuthToken".equals(name) || "getPreviousName".equals(name)) {
            return null;
        }
        if ("hasAccountAccess".equals(name) || "someUserHasAccount".equals(name) ||
            "accountAuthenticated".equals(name) || "addAccountExplicitly".equals(name) ||
            "addAccountExplicitlyWithVisibility".equals(name) ||
            "removeAccountExplicitly".equals(name) || "setAccountVisibility".equals(name)) {
            return Boolean.FALSE;
        }
        if ("getAccountVisibility".equals(name)) {
            return Integer.valueOf(AccountManager.VISIBILITY_NOT_VISIBLE);
        }
        if ("registerAccountListener".equals(name) || "unregisterAccountListener".equals(name)) {
            // AccountManager delivers the initial empty snapshot itself. No account
            // mutations are supported, so there are no subsequent change events.
            return null;
        }
        Object response = null;
        Class<?>[] types = method.getParameterTypes();
        for (int i = 0; args != null && i < types.length; i++) {
            if ("android.accounts.IAccountManagerResponse".equals(types[i].getName())) {
                response = args[i];
                break;
            }
        }
        if (response != null) {
            Class<?> type = Class.forName("android.accounts.IAccountManagerResponse");
            if ("getAccountsByFeatures".equals(name)) {
                Bundle result = new Bundle();
                result.putParcelableArray(AccountManager.KEY_ACCOUNTS, new Account[0]);
                type.getMethod("onResult", Bundle.class).invoke(response, result);
            } else if ("hasFeatures".equals(name) || "removeAccount".equals(name) ||
                       "removeAccountAsUser".equals(name)) {
                Bundle result = new Bundle();
                result.putBoolean(AccountManager.KEY_BOOLEAN_RESULT, false);
                type.getMethod("onResult", Bundle.class).invoke(response, result);
            } else {
                type.getMethod("onError", Integer.TYPE, String.class).invoke(response,
                    AccountManager.ERROR_CODE_UNSUPPORTED_OPERATION,
                    "Muplar account operation is not implemented: " + name);
            }
            return null;
        }
        throw new UnsupportedOperationException("Muplar account operation is not implemented: " + name);
    }
}
