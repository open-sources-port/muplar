package android.view;

import com.muplar.runtime.HostUi;
import android.util.FloatProperty;
import android.util.Property;
import android.content.Context;
import android.content.res.Resources;
import android.util.AttributeSet;
import android.graphics.drawable.Drawable;
import android.content.res.TypedArray;
import android.os.Handler;
import android.os.Binder;
import android.os.IBinder;
import android.graphics.Rect;
import android.graphics.Matrix;

public class View implements Drawable.Callback {
    public static class MeasureSpec {
        public static final int UNSPECIFIED = 0 << 30;
        public static final int EXACTLY = 1 << 30;
        public static final int AT_MOST = 2 << 30;
        public static int makeMeasureSpec(int size, int mode) {
            return (size & 0x3fffffff) | (mode & 0xc0000000);
        }
        public static int getMode(int spec) { return spec & 0xc0000000; }
        public static int getSize(int spec) { return spec & 0x3fffffff; }
    }
    private static final IBinder WINDOW_TOKEN = new Binder();
    private static final Display DISPLAY = new Display();
    private static final WindowInsetsController WINDOW_INSETS_CONTROLLER =
        new WindowInsetsController();
    private final Matrix matrix = new Matrix();
    protected static final int[] EMPTY_STATE_SET = new int[0];
    protected static final int[] ENABLED_STATE_SET = new int[]{16842910};
    public static final int VISIBLE = 0;
    public static final int INVISIBLE = 4;
    public static final int GONE = 8;
    public static final int OVER_SCROLL_ALWAYS = 0;
    public static final int OVER_SCROLL_IF_CONTENT_SCROLLS = 1;
    public static final int OVER_SCROLL_NEVER = 2;
    public static final int IMPORTANT_FOR_AUTOFILL_AUTO = 0;
    public static final int IMPORTANT_FOR_AUTOFILL_YES = 1;
    public static final int IMPORTANT_FOR_AUTOFILL_NO = 2;
    public static final int IMPORTANT_FOR_AUTOFILL_YES_EXCLUDE_DESCENDANTS = 4;
    public static final int IMPORTANT_FOR_AUTOFILL_NO_EXCLUDE_DESCENDANTS = 8;
    public static final int IMPORTANT_FOR_ACCESSIBILITY_AUTO = 0;
    public static final int IMPORTANT_FOR_ACCESSIBILITY_YES = 1;
    public static final int IMPORTANT_FOR_ACCESSIBILITY_NO = 2;
    public static final int IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS = 4;
    public static final int LAYER_TYPE_NONE = 0;
    public static final int LAYER_TYPE_SOFTWARE = 1;
    public static final int LAYER_TYPE_HARDWARE = 2;
    public static final int LAYOUT_DIRECTION_LTR = 0;
    public static final int LAYOUT_DIRECTION_RTL = 1;
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
    public interface OnLongClickListener { boolean onLongClick(View view); }
    public interface OnFocusChangeListener {
        void onFocusChange(View view, boolean hasFocus);
    }
    public interface OnTouchListener {
        boolean onTouch(View view, MotionEvent event);
    }
    public interface OnDragListener {
        boolean onDrag(View view, DragEvent event);
    }
    public static class DragShadowBuilder {
        private final View view;
        public DragShadowBuilder() { this(null); }
        public DragShadowBuilder(View view) { this.view = view; }
        public View getView() { return view; }
        public void onProvideShadowMetrics(android.graphics.Point size,
                android.graphics.Point touch) {}
        public void onDrawShadow(android.graphics.Canvas canvas) {}
    }
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
    private OnClickListener clickListener;
    private OnLongClickListener longClickListener;
    private OnFocusChangeListener focusChangeListener;
    private OnTouchListener touchListener;
    private OnDragListener dragListener;
    private boolean focused;
    private boolean pressed;
    private boolean selected;
    private android.view.animation.Animation animation;
    private boolean willNotDraw;
    private boolean hapticFeedbackEnabled = true;
    private boolean defaultFocusHighlightEnabled = true;
    private boolean focusable;
    private boolean scrollContainer;
    private int overScrollMode = OVER_SCROLL_IF_CONTENT_SCROLLS;
    private int importantForAutofill = IMPORTANT_FOR_AUTOFILL_AUTO;
    private int importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_AUTO;
    private AccessibilityDelegate accessibilityDelegate;
    private Object tag;
    private final java.util.Map<Integer, Object> keyedTags =
        new java.util.HashMap<Integer, Object>();
    private Drawable background;
    private int systemUiVisibility;
    private int scrollX;
    private int scrollY;
    private int paddingLeft;
    private int paddingTop;
    private int paddingRight;
    private int paddingBottom;
    private final Handler handler = new Handler();
    private boolean layoutRequested;
    private boolean clickable;
    private boolean longClickable;
    private Rect clipBounds;
    private java.util.List<Rect> systemGestureExclusionRects =
        java.util.Collections.emptyList();
    private int left;
    private int top;
    private int right;
    private int bottom;
    private int measuredWidth;
    private int measuredHeight;
    private int layerType;
    private float pivotX = Float.NaN;
    private float pivotY = Float.NaN;
    private int backgroundColor;
    private ViewParent parent;
    private WindowInsetsAnimation.Callback windowInsetsAnimationCallback;
    private int layoutDirection = -1;
    private CharSequence contentDescription;
    private CharSequence accessibilityPaneTitle;

    protected View(Object peer) { this(peer, null); }
    protected View(Object peer, Context context) {
        this.peer = peer;
        this.context = context;
    }
    public View(Context context) { this(HostUi.createLinearLayout(), context); }
    public View(Context context, AttributeSet attributes) { this(context); }
    public View(Context context, AttributeSet attributes, int defStyleAttr) {
        this(context);
    }
    public View(Context context, AttributeSet attributes, int defStyleAttr,
            int defStyleRes) { this(context); }
    public final Object getPeer() { return peer; }
    public Context getContext() { return context; }
    public IBinder getWindowToken() { return WINDOW_TOKEN; }
    public Display getDisplay() { return DISPLAY; }
    public WindowInsetsController getWindowInsetsController() {
        return WINDOW_INSETS_CONTROLLER;
    }
    public WindowInsets getRootWindowInsets() { return new WindowInsets(); }
    public Matrix getMatrix() { return matrix; }
    public Resources getResources() {
        return context == null ? Resources.getSystem() : context.getResources();
    }
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
    public void setElevation(float value) { properties[11] = value; }
    public float getElevation() { return properties[11]; }
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
    public void setVisibility(int visibility) {
        this.visibility = visibility;
        HostUi.setVisibility(peer, visibility == VISIBLE);
    }
    public int getVisibility() { return visibility; }
    public int getWindowVisibility() { return visibility; }
    public boolean isShown() {
        if (visibility != VISIBLE) return false;
        ViewParent current = parent;
        while (current instanceof View) {
            if (((View) current).getVisibility() != VISIBLE) return false;
            current = ((View) current).getParent();
        }
        return true;
    }
    public ViewTreeObserver getViewTreeObserver() { return viewTreeObserver; }
    public void setLayoutParams(ViewGroup.LayoutParams params) { layoutParams = params; }
    public ViewGroup.LayoutParams getLayoutParams() { return layoutParams; }
    public int getWidth() {
        if (right > left) return right - left;
        return layoutParams != null && layoutParams.width > 0 ? layoutParams.width
            : getResources().getDisplayMetrics().widthPixels;
    }
    public int getHeight() {
        if (bottom > top) return bottom - top;
        return layoutParams != null && layoutParams.height > 0 ? layoutParams.height
            : getResources().getDisplayMetrics().heightPixels;
    }
    public int getMeasuredWidth() { return measuredWidth > 0 ? measuredWidth : getWidth(); }
    public int getMeasuredHeight() { return measuredHeight > 0 ? measuredHeight : getHeight(); }
    public final void measure(int widthMeasureSpec, int heightMeasureSpec) {
        onMeasure(widthMeasureSpec, heightMeasureSpec);
    }
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        setMeasuredDimension(MeasureSpec.getSize(widthMeasureSpec),
            MeasureSpec.getSize(heightMeasureSpec));
    }
    protected final void setMeasuredDimension(int width, int height) {
        measuredWidth = Math.max(0, width);
        measuredHeight = Math.max(0, height);
    }
    public int getLeft() { return left; }
    public int getTop() { return top; }
    public void getHitRect(Rect outRect) {
        if (outRect != null) outRect.set(left, top, right, bottom);
    }
    public void getDrawingRect(Rect outRect) {
        if (outRect != null) outRect.set(0, 0, getWidth(), getHeight());
    }
    public void getLocationOnScreen(int[] location) {
        if (location == null || location.length < 2)
            throw new IllegalArgumentException("location must contain two values");
        int x = left;
        int y = top;
        ViewParent current = parent;
        while (current instanceof View) {
            View ancestor = (View) current;
            x += ancestor.getLeft() - ancestor.getScrollX();
            y += ancestor.getTop() - ancestor.getScrollY();
            current = ancestor.getParent();
        }
        location[0] = x;
        location[1] = y;
    }
    public void getLocationInWindow(int[] location) { getLocationOnScreen(location); }
    public int getRight() { return right > left ? right : left + getWidth(); }
    public int getBottom() { return bottom > top ? bottom : top + getHeight(); }
    public void layout(int left, int top, int right, int bottom) {
        this.left = left;
        this.top = top;
        this.right = right;
        this.bottom = bottom;
        layoutRequested = false;
    }
    public void offsetLeftAndRight(int offset) { left += offset; right += offset; }
    public void offsetTopAndBottom(int offset) { top += offset; bottom += offset; }
    public void setLayerType(int type, android.graphics.Paint paint) { layerType = type; }
    public int getLayerType() { return layerType; }
    public void setPivotX(float value) { pivotX = value; }
    public float getPivotX() { return Float.isNaN(pivotX) ? getWidth() * 0.5f : pivotX; }
    public void setPivotY(float value) { pivotY = value; }
    public float getPivotY() { return Float.isNaN(pivotY) ? getHeight() * 0.5f : pivotY; }
    public void setContentDescription(CharSequence description) {
        contentDescription = description;
    }
    public CharSequence getContentDescription() { return contentDescription; }
    public void setAccessibilityPaneTitle(CharSequence title) {
        accessibilityPaneTitle = title;
    }
    public CharSequence getAccessibilityPaneTitle() { return accessibilityPaneTitle; }
    public void addOnAttachStateChangeListener(OnAttachStateChangeListener listener) {
        if (listener != null) attachListeners.add(listener);
    }
    public void removeOnAttachStateChangeListener(OnAttachStateChangeListener listener) {
        attachListeners.remove(listener);
    }
    public boolean isAttachedToWindow() { return true; }
    public void invalidate() {}
    public void requestLayout() { layoutRequested = true; }
    public void forceLayout() { layoutRequested = true; }
    public boolean isLayoutRequested() { return layoutRequested; }
    public void setClipBounds(Rect bounds) {
        clipBounds = bounds == null ? null : new Rect(bounds);
    }
    public Rect getClipBounds() {
        return clipBounds == null ? null : new Rect(clipBounds);
    }
    protected boolean awakenScrollBars() { return false; }
    protected boolean awakenScrollBars(int startDelay) { return false; }
    protected boolean awakenScrollBars(int startDelay, boolean invalidate) {
        return false;
    }
    public void setSystemGestureExclusionRects(java.util.List<Rect> rects) {
        systemGestureExclusionRects = rects == null
            ? java.util.Collections.<Rect>emptyList()
            : new java.util.ArrayList<Rect>(rects);
    }
    public java.util.List<Rect> getSystemGestureExclusionRects() {
        return java.util.Collections.unmodifiableList(systemGestureExclusionRects);
    }
    public static void setTraceLayoutSteps(boolean enabled) {}
    public static void setTraceRequestLayoutClass(boolean enabled) {}
    public static void setTracedRequestLayoutClassClass(String className) {}
    public void setOnClickListener(OnClickListener listener) {
        clickListener = listener;
        clickable = listener != null;
        HostUi.setOnViewClickListener(peer, listener == null ? null : new Runnable() {
            @Override public void run() { performClick(); }
        });
    }
    public void setOnLongClickListener(OnLongClickListener listener) {
        longClickListener = listener;
        longClickable = listener != null;
    }
    public void setOnFocusChangeListener(OnFocusChangeListener listener) {
        focusChangeListener = listener;
    }
    public void setOnTouchListener(OnTouchListener listener) {
        touchListener = listener;
    }
    public void setOnDragListener(OnDragListener listener) { dragListener = listener; }
    public boolean dispatchDragEvent(DragEvent event) {
        return dragListener != null && dragListener.onDrag(this, event);
    }
    public boolean startDragAndDrop(android.content.ClipData data,
            DragShadowBuilder shadowBuilder, Object localState, int flags) {
        return dispatchDragEvent(DragEvent.obtain(DragEvent.ACTION_DRAG_STARTED,
            0, 0, localState, data == null ? null : data.getDescription(), data, false));
    }
    public boolean startDrag(android.content.ClipData data,
            DragShadowBuilder shadowBuilder, Object localState, int flags) {
        return startDragAndDrop(data, shadowBuilder, localState, flags);
    }
    public boolean dispatchTouchEvent(MotionEvent event) {
        if (touchListener != null && touchListener.onTouch(this, event)) return true;
        return onTouchEvent(event);
    }
    public boolean onTouchEvent(MotionEvent event) {
        if (!clickable || event == null) return false;
        if (event.getActionMasked() == MotionEvent.ACTION_UP) performClick();
        return true;
    }
    public boolean performClick() {
        if (clickListener == null) return false;
        clickListener.onClick(this); return true;
    }
    public void refreshDrawableState() { drawableStateChanged(); }
    protected void drawableStateChanged() {}
    public int[] getDrawableState() { return ENABLED_STATE_SET.clone(); }
    public void setClickable(boolean value) { clickable = value; }
    public void setPressed(boolean value) { pressed = value; }
    public boolean isPressed() { return pressed; }
    public void setSelected(boolean value) { selected = value; }
    public boolean isSelected() { return selected; }
    public void startAnimation(android.view.animation.Animation value) {
        animation = value;
        if (value != null) value.start();
    }
    public void clearAnimation() {
        if (animation != null) animation.cancel();
        animation = null;
    }
    public android.view.animation.Animation getAnimation() { return animation; }
    public ViewRootImpl getViewRootImpl() { return null; }
    public boolean isClickable() { return clickable; }
    public void setLongClickable(boolean value) { longClickable = value; }
    public boolean isLongClickable() { return longClickable; }
    public boolean performLongClick() {
        return longClickListener != null && longClickListener.onLongClick(this);
    }
    public void cancelLongPress() {}
    public boolean requestFocus() {
        if (!focused) {
            focused = true;
            if (focusChangeListener != null) focusChangeListener.onFocusChange(this, true);
        }
        return true;
    }
    public void clearFocus() {
        if (focused) {
            focused = false;
            if (focusChangeListener != null) focusChangeListener.onFocusChange(this, false);
        }
    }
    public boolean hasFocus() { return focused; }
    public boolean hasWindowFocus() { return isShown(); }
    public boolean performHapticFeedback(int feedbackConstant, int flags) {
        return performHapticFeedback(feedbackConstant);
    }
    public void sendAccessibilityEvent(int eventType) {}
    public void setWillNotDraw(boolean value) { willNotDraw = value; }
    public boolean willNotDraw() { return willNotDraw; }
    public void setHapticFeedbackEnabled(boolean enabled) {
        hapticFeedbackEnabled = enabled;
    }
    public boolean isHapticFeedbackEnabled() { return hapticFeedbackEnabled; }
    public boolean performHapticFeedback(int feedbackConstant) {
        return hapticFeedbackEnabled;
    }
    public void setDefaultFocusHighlightEnabled(boolean enabled) {
        defaultFocusHighlightEnabled = enabled;
    }
    public boolean getDefaultFocusHighlightEnabled() {
        return defaultFocusHighlightEnabled;
    }
    public void setFocusable(boolean value) { focusable = value; }
    public boolean isFocusable() { return focusable; }
    public void setFocusableInTouchMode(boolean value) { focusable = value; }
    public void setScrollContainer(boolean value) { scrollContainer = value; }
    public boolean isScrollContainer() { return scrollContainer; }
    public void setOverScrollMode(int mode) { overScrollMode = mode; }
    public int getOverScrollMode() { return overScrollMode; }
    public void setImportantForAutofill(int mode) { importantForAutofill = mode; }
    public int getImportantForAutofill() { return importantForAutofill; }
    public void setImportantForAccessibility(int mode) {
        importantForAccessibility = mode;
    }
    public int getImportantForAccessibility() { return importantForAccessibility; }
    public void setAccessibilityDelegate(AccessibilityDelegate delegate) {
        accessibilityDelegate = delegate;
    }
    public AccessibilityDelegate getAccessibilityDelegate() {
        return accessibilityDelegate;
    }
    public void setTag(Object value) { tag = value; }
    public Object getTag() { return tag; }
    public void setTag(int key, Object value) {
        if (value == null) keyedTags.remove(key); else keyedTags.put(key, value);
    }
    public Object getTag(int key) { return keyedTags.get(key); }
    public void setBackground(Drawable value) {
        if (background != null) background.setCallback(null);
        background = value;
        if (background != null) background.setCallback(this);
    }
    public Drawable getBackground() { return background; }
    public void setBackgroundResource(int resourceId) {
        setBackground(resourceId == 0 ? null : getResources().getDrawable(resourceId));
    }
    public void setBackgroundColor(int color) { backgroundColor = color; }
    public void draw(android.graphics.Canvas canvas) {
        if (background != null) background.draw(canvas);
        onDraw(canvas);
    }
    protected void onDraw(android.graphics.Canvas canvas) {}
    public void setSystemUiVisibility(int visibility) {
        systemUiVisibility = visibility;
    }
    public int getSystemUiVisibility() { return systemUiVisibility; }
    public int getScrollX() { return scrollX; }
    public int getScrollY() { return scrollY; }
    public void scrollTo(int x, int y) { scrollX = x; scrollY = y; }
    public void scrollBy(int x, int y) { scrollTo(scrollX + x, scrollY + y); }
    public void setPadding(int left, int top, int right, int bottom) {
        paddingLeft = left;
        paddingTop = top;
        paddingRight = right;
        paddingBottom = bottom;
    }
    public int getPaddingLeft() { return paddingLeft; }
    public int getPaddingTop() { return paddingTop; }
    public int getPaddingRight() { return paddingRight; }
    public int getPaddingBottom() { return paddingBottom; }
    public int getPaddingStart() {
        return getLayoutDirection() == LAYOUT_DIRECTION_RTL ? paddingRight : paddingLeft;
    }
    public int getPaddingEnd() {
        return getLayoutDirection() == LAYOUT_DIRECTION_RTL ? paddingLeft : paddingRight;
    }
    public ViewParent getParent() { return parent; }
    public View getRootView() {
        View root = this;
        ViewParent current = parent;
        while (current instanceof View) {
            root = (View) current;
            current = root.getParent();
        }
        return root;
    }
    public boolean getLocalVisibleRect(Rect rectangle) {
        if (rectangle == null || visibility != VISIBLE) return false;
        rectangle.set(0, 0, getWidth(), getHeight());
        return !rectangle.isEmpty();
    }
    public void setWindowInsetsAnimationCallback(
            WindowInsetsAnimation.Callback callback) {
        windowInsetsAnimationCallback = callback;
    }
    void assignParent(ViewParent value) { parent = value; }
    public boolean post(Runnable action) { return handler.post(action); }
    public boolean postDelayed(Runnable action, long delayMillis) {
        return handler.postDelayed(action, delayMillis);
    }
    public boolean removeCallbacks(Runnable action) {
        handler.removeCallbacks(action);
        return true;
    }
    public void setLayoutDirection(int direction) { layoutDirection = direction; }
    public int getLayoutDirection() {
        return layoutDirection >= 0 ? layoutDirection
            : getResources().getConfiguration().getLayoutDirection();
    }
    public void invalidateDrawable(Drawable drawable) { invalidate(); }
    public void scheduleDrawable(Drawable drawable, Runnable action, long when) {
        if (action != null) action.run();
    }
    public void unscheduleDrawable(Drawable drawable, Runnable action) {}
    public View findViewById(int wantedId) {
        return id == wantedId ? this : null;
    }
    protected void onFinishInflate() {}
    public void saveAttributeDataForStyleable(Context context, int[] styleable,
            AttributeSet attributes, TypedArray typedArray, int defStyleAttr,
            int defStyleRes) {}

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
