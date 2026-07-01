package android.view;

import android.content.Context;
import android.content.res.Resources;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.Deque;
import java.lang.reflect.Constructor;
import android.util.AttributeSet;

public class LayoutInflater {
    private static final int START_ELEMENT = 0x0102;
    private static final int END_ELEMENT = 0x0103;
    private static final int TYPE_REFERENCE = 0x01;
    private static final int TYPE_STRING = 0x03;
    private final Context context;

    private LayoutInflater(Context context) { this.context = context; }
    public static LayoutInflater from(Context context) {
        return new LayoutInflater(context);
    }

    public View inflate(int resourceId, ViewGroup root) {
        byte[] xml = context.getResources().readResourceFile(resourceId);
        View inflated = inflateBinary(xml);
        if (root != null) root.addView(inflated);
        return inflated;
    }

    private View inflateBinary(byte[] xml) {
        if (xml.length < 8 || u16(xml, 0) != 0x0003)
            throw new IllegalArgumentException("compiled Android XML required");
        String[] strings = new String[0];
        Deque<View> stack = new ArrayDeque<View>();
        View root = null;
        int offset = 8;
        while (offset + 8 <= xml.length) {
            int type = u16(xml, offset);
            int header = u16(xml, offset + 2);
            int size = u32(xml, offset + 4);
            if (size < header || offset + size > xml.length) break;
            if (type == 0x0001) {
                strings = stringPool(xml, offset);
            } else if (type == START_ELEMENT && offset + 36 <= xml.length) {
                int nameIndex = u32(xml, offset + 20);
                if (nameIndex < 0 || nameIndex >= strings.length)
                    throw new IllegalArgumentException("layout tag is invalid");
                View view = createView(strings[nameIndex]);
                applyAttributes(view, xml, offset, size, strings);
                if (!stack.isEmpty()) {
                    View parent = stack.peek();
                    if (!(parent instanceof ViewGroup))
                        throw new IllegalArgumentException(
                            "layout parent is not a ViewGroup");
                    ((ViewGroup) parent).addView(view);
                } else {
                    root = view;
                }
                stack.push(view);
            } else if (type == END_ELEMENT && !stack.isEmpty()) {
                stack.pop();
            }
            offset += size;
        }
        if (root == null) throw new IllegalArgumentException("layout is empty");
        return root;
    }

    private View createView(String name) {
        if (name.endsWith("LinearLayout")) return new LinearLayout(context);
        if (name.endsWith("TextView")) return new TextView(context);
        if (name.endsWith("Button")) return new Button(context);
        if (name.endsWith("CheckBox")) return new CheckBox(context);
        if (name.endsWith("ImageView")) return new ImageView(context);
        if (name.endsWith("ListView")) return new ListView(context);
        try {
            ClassLoader loader = Thread.currentThread().getContextClassLoader();
            Class<?> type = Class.forName(name, true, loader);
            if (!View.class.isAssignableFrom(type))
                throw new IllegalArgumentException("layout tag is not a View: " + name);
            try {
                Constructor<?> constructor = type.getConstructor(
                    Context.class, AttributeSet.class);
                return (View) constructor.newInstance(context, null);
            } catch (NoSuchMethodException ignored) {
                Constructor<?> constructor = type.getConstructor(Context.class);
                return (View) constructor.newInstance(context);
            }
        } catch (ReflectiveOperationException error) {
            throw new IllegalArgumentException(
                "cannot inflate layout view: " + name, error);
        }
    }

    private void applyAttributes(View view, byte[] xml, int element,
                                 int chunkSize, String[] strings) {
        int attributeStart = u16(xml, element + 24);
        int attributeSize = u16(xml, element + 26);
        int attributeCount = u16(xml, element + 28);
        int attributes = element + 16 + attributeStart;
        for (int i = 0; i < attributeCount; ++i) {
            int attribute = attributes + i * attributeSize;
            if (attribute + 20 > element + chunkSize) break;
            int nameIndex = u32(xml, attribute + 4);
            if (nameIndex < 0 || nameIndex >= strings.length) continue;
            String name = strings[nameIndex];
            int rawIndex = u32(xml, attribute + 8);
            int valueType = xml[attribute + 15] & 0xff;
            int data = u32(xml, attribute + 16);
            String raw = rawIndex >= 0 && rawIndex < strings.length
                ? strings[rawIndex] : null;

            if ("id".equals(name)) {
                view.setId(data);
            } else if ("enabled".equals(name)) {
                view.setEnabled(data != 0);
            } else if ("text".equals(name) && view instanceof TextView) {
                ((TextView) view).setText(textValue(raw, valueType, data, strings));
            } else if ("orientation".equals(name) &&
                       view instanceof LinearLayout) {
                ((LinearLayout) view).setOrientation(data == 0
                    ? LinearLayout.HORIZONTAL : LinearLayout.VERTICAL);
            } else if ("checked".equals(name) && view instanceof CheckBox) {
                ((CheckBox) view).setChecked(data != 0);
            } else if ("src".equals(name) && view instanceof ImageView &&
                       valueType == TYPE_REFERENCE) {
                ((ImageView) view).setImageResource(data);
            }
        }
    }

    private CharSequence textValue(String raw, int valueType, int data,
                                   String[] strings) {
        if (valueType == TYPE_REFERENCE)
            return context.getResources().getString(data);
        if (valueType == TYPE_STRING && data >= 0 && data < strings.length)
            return strings[data];
        return raw == null ? "" : raw;
    }

    private static String[] stringPool(byte[] data, int offset) {
        int header = u16(data, offset + 2);
        int count = u32(data, offset + 8);
        boolean utf8 = (u32(data, offset + 16) & 0x100) != 0;
        int stringsStart = u32(data, offset + 20);
        String[] result = new String[count];
        for (int i = 0; i < count; ++i) {
            int position = offset + stringsStart +
                u32(data, offset + header + i * 4);
            if (utf8) {
                int[] chars = length8(data, position);
                int[] bytes = length8(data, chars[1]);
                result[i] = new String(data, bytes[1], bytes[0],
                    StandardCharsets.UTF_8);
            } else {
                int[] length = length16(data, position);
                result[i] = new String(data, length[1], length[0] * 2,
                    StandardCharsets.UTF_16LE);
            }
        }
        return result;
    }

    private static int[] length8(byte[] data, int position) {
        int first = data[position++] & 0xff;
        if ((first & 0x80) == 0) return new int[]{first, position};
        return new int[]{((first & 0x7f) << 8) | (data[position++] & 0xff),
            position};
    }
    private static int[] length16(byte[] data, int position) {
        int first = u16(data, position); position += 2;
        if ((first & 0x8000) == 0) return new int[]{first, position};
        return new int[]{((first & 0x7fff) << 16) | u16(data, position),
            position + 2};
    }
    private static int u16(byte[] data, int offset) {
        return (data[offset] & 0xff) | ((data[offset + 1] & 0xff) << 8);
    }
    private static int u32(byte[] data, int offset) {
        return (data[offset] & 0xff) | ((data[offset + 1] & 0xff) << 8) |
            ((data[offset + 2] & 0xff) << 16) | (data[offset + 3] << 24);
    }
}
