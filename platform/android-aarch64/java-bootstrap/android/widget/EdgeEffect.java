package android.widget;

import android.content.Context;
import android.graphics.Canvas;
import android.util.AttributeSet;

public class EdgeEffect {
    private float distance;
    public EdgeEffect(Context context) {}
    public EdgeEffect(Context context, AttributeSet attributes) {}
    public void setSize(int width, int height) {}
    public void setColor(int color) {}
    public int getColor() { return 0; }
    public boolean isFinished() { return distance == 0f; }
    public void finish() { distance = 0f; }
    public void onPull(float deltaDistance) { onPull(deltaDistance, 0.5f); }
    public void onPull(float deltaDistance, float displacement) {
        distance = Math.max(0f, distance + deltaDistance);
    }
    public float onPullDistance(float deltaDistance, float displacement) {
        onPull(deltaDistance, displacement); return deltaDistance;
    }
    public float getDistance() { return distance; }
    public void onRelease() { distance = 0f; }
    public void onAbsorb(int velocity) { distance = velocity == 0 ? 0f : 1f; }
    public boolean draw(Canvas canvas) { return !isFinished(); }
}
