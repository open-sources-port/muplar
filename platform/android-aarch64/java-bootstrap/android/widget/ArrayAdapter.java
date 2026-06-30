package android.widget;

import android.content.Context;
import android.view.View;
import android.view.ViewGroup;
import java.util.Arrays;
import java.util.List;

public class ArrayAdapter<T> extends BaseAdapter {
    private final Context context;
    private final List<T> items;

    public ArrayAdapter(Context context, int resource, T[] items) {
        this.context = context;
        this.items = Arrays.asList(items);
    }
    public ArrayAdapter(Context context, int resource, List<T> items) {
        this.context = context;
        this.items = items;
    }
    public int getCount() { return items.size(); }
    public T getItem(int position) { return items.get(position); }
    public View getView(int position, View convertView, ViewGroup parent) {
        TextView text = convertView instanceof TextView
            ? (TextView) convertView : new TextView(context);
        T item = getItem(position);
        text.setText(item == null ? "" : item.toString());
        return text;
    }
}
