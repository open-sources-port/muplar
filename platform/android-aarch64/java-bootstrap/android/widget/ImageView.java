package android.widget;

import android.content.Context;
import android.graphics.drawable.Drawable;
import android.view.View;
import com.muplar.runtime.HostUi;

public class ImageView extends View {
    private final Context context;
    public ImageView(Context context) {
        super(HostUi.createImageView(), context);
        this.context = context;
    }
    public void setImageDrawable(Drawable drawable) {
        HostUi.setImage(getPeer(), drawable == null ? null :
            drawable.getSourcePath());
    }
    public void setImageResource(int id) {
        setImageDrawable(context.getResources().getDrawable(id));
    }
}
