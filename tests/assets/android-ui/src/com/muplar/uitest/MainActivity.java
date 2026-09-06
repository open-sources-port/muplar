package com.muplar.uitest;

import android.app.Activity;
import android.content.res.Configuration;
import android.os.Bundle;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.ListView;
import android.widget.TextView;

public class MainActivity extends Activity {
    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (!UiTestApplication.isCreated() ||
            !(getApplicationContext() instanceof UiTestApplication)) {
            throw new AssertionError("Application lifecycle did not run first");
        }
        setTitle(getString(R.string.app_name));
        setContentView(R.layout.activity_main);
        final TextView status = (TextView) findViewById(R.id.status);
        Button action = (Button) findViewById(R.id.action);
        action.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) {
                status.setText("Input received");
                System.out.println("[UiTest] button input received");
            }
        });
        action.setOnTouchListener(new View.OnTouchListener() {
            @Override public boolean onTouch(View v, android.view.MotionEvent ev) {
                System.out.println("[UiTest] button onTouch: action=" + ev.getAction() + " x=" + ev.getX() + " y=" + ev.getY());
                return false;
            }
        });
        action.post(new Runnable() {
            @Override public void run() {
                int[] loc = new int[2];
                action.getLocationOnScreen(loc);
                System.out.println("[UiTest] button screen coords: x=" + loc[0] + ", y=" + loc[1] + ", w=" + action.getWidth() + ", h=" + action.getHeight());
            }
        });
        ListView list = (ListView) findViewById(R.id.list);
        list.setAdapter(new ArrayAdapter<String>(this,
            android.R.layout.simple_list_item_1,
            new String[]{"Strings", "Colors", "Dimensions", "Lists"}));
        list.setOnItemClickListener(new android.widget.AdapterView.OnItemClickListener() {
            @Override public void onItemClick(android.widget.AdapterView<?> parent, View view, int position, long id) {
                System.out.println("[UiTest] list item clicked: " + position);
                launchSecondActivity();
            }
        });
        System.out.println("[UiTest] XML layout inflated");
    }

    public void launchSecondActivity() {
        System.out.println("[UiTest] launching SecondActivity");
        startActivity(new android.content.Intent(this, SecondActivity.class));
    }

    @Override public boolean dispatchTouchEvent(android.view.MotionEvent ev) {
        System.out.println("[UiTest] dispatchTouchEvent: action=" + ev.getAction() + " x=" + ev.getX() + " y=" + ev.getY());
        boolean res = super.dispatchTouchEvent(ev);
        System.out.println("[UiTest] dispatchTouchEvent: res=" + res);
        return res;
    }

    @Override public boolean onKeyDown(int keyCode, android.view.KeyEvent event) {
        if (keyCode == android.view.KeyEvent.KEYCODE_L || keyCode == android.view.KeyEvent.KEYCODE_SPACE) {
            launchSecondActivity();
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override protected void onRestart() {
        super.onRestart();
        System.out.println("[UiTest] onRestart");
    }
    @Override protected void onStart() {
        super.onStart();
        System.out.println("[UiTest] onStart");
    }
    @Override protected void onResume() {
        super.onResume();
        System.out.println("[UiTest] onResume");
    }
    @Override protected void onPause() {
        super.onPause();
        System.out.println("[UiTest] onPause");
    }
    @Override protected void onStop() {
        super.onStop();
        System.out.println("[UiTest] onStop");
    }
    @Override protected void onDestroy() {
        super.onDestroy();
        System.out.println("[UiTest] onDestroy");
    }
    @Override public void onWindowFocusChanged(boolean focused) {
        System.out.println("[UiTest] focus=" + focused);
    }
    @Override public void onConfigurationChanged(Configuration configuration) {
        System.out.println("[UiTest] orientation=" + configuration.orientation);
    }
}
