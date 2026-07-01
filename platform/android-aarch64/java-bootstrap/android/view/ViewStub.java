package android.view;

import android.content.Context;
import android.util.AttributeSet;

public final class ViewStub extends View {
    private int layoutResource;
    private View inflatedView;

    public ViewStub(Context context) { super(context); }
    public ViewStub(Context context, AttributeSet attributes) { super(context, attributes); }
    public ViewStub(Context context, AttributeSet attributes, int defStyleAttr) {
        super(context, attributes, defStyleAttr);
    }
    public void setLayoutResource(int resource) { layoutResource = resource; }
    public int getLayoutResource() { return layoutResource; }
    public View inflate() {
        if (inflatedView == null)
            inflatedView = layoutResource == 0 ? new View(getContext())
                : LayoutInflater.from(getContext()).inflate(layoutResource, null, false);
        return inflatedView;
    }
}
