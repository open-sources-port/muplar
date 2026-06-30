import android.content.res.Resources;
import android.graphics.drawable.Drawable;

public final class ResourcesSmoke {
    public static void main(String[] args) {
        Resources resources = new Resources();
        int stringId = (int) Long.parseLong(args[0], 16);
        int colorId = (int) Long.parseLong(args[1], 16);
        int dimensionId = (int) Long.parseLong(args[2], 16);
        int drawableId = (int) Long.parseLong(args[3], 16);
        Drawable drawable = resources.getDrawable(drawableId);
        System.out.println("string=" + resources.getString(stringId));
        System.out.println("color=0x" +
            Integer.toHexString(resources.getColor(colorId)));
        System.out.println("dimension=" + resources.getDimension(dimensionId));
        System.out.println("drawable=" + drawable.getSourcePath());
    }
}
