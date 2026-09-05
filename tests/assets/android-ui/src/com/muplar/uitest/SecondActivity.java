package com.muplar.uitest;

import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;

public class SecondActivity extends Activity {
    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        TextView tv = new TextView(this);
        tv.setText("Second Activity Content");
        setContentView(tv);
        System.out.println("[UiTest/Second] onCreate");
    }

    @Override protected void onStart() {
        super.onStart();
        System.out.println("[UiTest/Second] onStart");
    }

    @Override protected void onResume() {
        super.onResume();
        System.out.println("[UiTest/Second] onResume");
    }

    @Override public boolean onKeyDown(int keyCode, android.view.KeyEvent event) {
        if (keyCode == android.view.KeyEvent.KEYCODE_F) {
            System.out.println("[UiTest/Second] key F received, calling finish()");
            finish();
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override protected void onPause() {
        super.onPause();
        System.out.println("[UiTest/Second] onPause");
    }

    @Override protected void onStop() {
        super.onStop();
        System.out.println("[UiTest/Second] onStop");
    }

    @Override protected void onDestroy() {
        super.onDestroy();
        System.out.println("[UiTest/Second] onDestroy");
    }
}
