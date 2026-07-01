package android.view;

import com.muplar.runtime.HostUi;
import android.util.FloatProperty;
import android.util.Property;
import android.content.Context;

public class View {
    public static final int VISIBLE = 0;
    public static final int INVISIBLE = 4;
    public static final int GONE = 8;
    public static final Property<View, Float> ALPHA = floatProperty("alpha", 0);
    public static final Property<View, Float> TRANSLATION_X =
        floatProperty("translationX", 1);
    public static final Property<View, Float> TRANSLATION_Y =
        floatProperty("translationY", 2);
    public static final Property<View, Float> TRANSLATION_Z =
        floatProperty("translationZ", 3);
    public static final Property<View, Float> SCALE_X = floatProperty("scaleX", 4);
    public static final Property<View, Float> SCALE_Y = floatProperty("scaleY", 5);
    public static final Property<View, Float> ROTATION = floatProperty("rotation", 6);
    public static final Property<View, Float> ROTATION_X =
        floatProperty("rotationX", 7);
    public static final Property<View, Float> ROTATION_Y =
        floatProperty("rotationY", 8);
    public static final Property<View, Float> X = floatProperty("x", 9);
    public static final Property<View, Float> Y = floatProperty("y", 10);
    public static final Property<View, Float> Z = floatProperty("z", 11);

    public interface OnClickListener { void onClick(View view); }
    public interface OnAttachStateChangeListener {
        void onViewAttachedToWindow(View view);
        void onViewDetachedFromWindow(View view);
    }
    public static class AccessibilityDelegate {}
    private final Object peer;
    private final Context context;
    private int id;
    private final float[] properties = {
        1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f
    };
    private int visibility = VISIBLE;
    private final ViewTreeObserver viewTreeObserver = new ViewTreeObserver();
    private ViewGroup.LayoutParams layoutParams;
    private final java.util.List<OnAttachStateChangeListener> attachListeners =
        new java.util.concurrent.CopyOnWriteArrayList<OnAttachStateChangeListener>();

    protected View(Object peer) { this(peer, null); }
    protected View(Object peer, Context context) {
        this.peer = peer;
        this.context = context;
    }
    public final Object getPeer() { return peer; }
    public Context getContext() { return context; }
    public void setEnabled(boolean enabled) { HostUi.setEnabled(peer, enabled); }
    public void setId(int id) { this.id = id; }
    public int getId() { return id; }
    public void setAlpha(float value) { properties[0] = value; }
    public float getAlpha() { return properties[0]; }
    public void setTranslationX(float value) { properties[1] = value; }
    public float getTranslationX() { return properties[1]; }
    public void setTranslationY(float value) { properties[2] = value; }
    public float getTranslationY() { return properties[2]; }
    public void setTranslationZ(float value) { properties[3] = value; }
    public float getTranslationZ() { return properties[3]; }
    public void setScaleX(float value) { properties[4] = value; }
    public float getScaleX() { return properties[4]; }
    public void setScaleY(float value) { properties[5] = value; }
    public float getScaleY() { return properties[5]; }
    public void setRotation(float value) { properties[6] = value; }
    public float getRotation() { return properties[6]; }
    public void setRotationX(float value) { properties[7] = value; }
    public float getRotationX() { return properties[7]; }
    public void setRotationY(float value) { properties[8] = value; }
    public float getRotationY() { return properties[8]; }
    public void setX(float value) { properties[9] = value; }
    public float getX() { return properties[9]; }
    public void setY(float value) { properties[10] = value; }
    public float getY() { return properties[10]; }
    public void setZ(float value) { properties[11] = value; }
    public float getZ() { return properties[11]; }
    public void setVisibility(int visibility) { this.visibility = visibility; }
    public int getVisibility() { return visibility; }
    public ViewTreeObserver getViewTreeObserver() { return viewTreeObserver; }
    public void setLayoutParams(ViewGroup.LayoutParams params) { layoutParams = params; }
    public ViewGroup.LayoutParams getLayoutParams() { return layoutParams; }
    public void addOnAttachStateChangeListener(OnAttachStateChangeListener listener) {
        if (listener != null) attachListeners.add(listener);
    }
    public void removeOnAttachStateChangeListener(OnAttachStateChangeListener listener) {
        attachListeners.remove(listener);
    }
    public boolean isAttachedToWindow() { return true; }
    public void invalidate() {}
    public View findViewById(int wantedId) {
        return id == wantedId ? this : null;
    }

    private static Property<View, Float> floatProperty(
            String name, final int index) {
        return new FloatProperty<View>(name) {
            @Override public Float get(View view) {
                return Float.valueOf(view.properties[index]);
            }
            @Override public void setValue(View view, float value) {
                view.properties[index] = value;
            }
        };
    }
}
