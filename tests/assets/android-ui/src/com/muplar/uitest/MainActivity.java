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
        ListView list = (ListView) findViewById(R.id.list);
        list.setAdapter(new ArrayAdapter<String>(this,
            android.R.layout.simple_list_item_1,
            new String[]{"Strings", "Colors", "Dimensions", "Lists"}));
        System.out.println("[UiTest] XML layout inflated");
    }

    @Override protected void onStart() {
        System.out.println("[UiTest] onStart");
    }
    @Override protected void onResume() {
        System.out.println("[UiTest] onResume");
    }
    @Override public void onWindowFocusChanged(boolean focused) {
        System.out.println("[UiTest] focus=" + focused);
    }
    @Override public void onConfigurationChanged(Configuration configuration) {
        System.out.println("[UiTest] orientation=" + configuration.orientation);
    }
}
