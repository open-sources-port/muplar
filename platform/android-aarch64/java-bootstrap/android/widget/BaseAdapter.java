package android.widget;

import android.view.View;
import android.view.ViewGroup;

public abstract class BaseAdapter {
    public abstract int getCount();
    public abstract Object getItem(int position);
    public abstract View getView(int position, View convertView, ViewGroup parent);
}
