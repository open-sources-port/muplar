package android.widget;

import android.content.Context;
import android.view.animation.Interpolator;

public class OverScroller {
    private int currentX, currentY, finalX, finalY;
    private boolean finished = true;
    public OverScroller(Context context) {}
    public OverScroller(Context context, Interpolator interpolator) {}
    public void startScroll(int startX, int startY, int dx, int dy) {
        startScroll(startX, startY, dx, dy, 250);
    }
    public void startScroll(int startX, int startY, int dx, int dy, int duration) {
        currentX = startX; currentY = startY;
        finalX = startX + dx; finalY = startY + dy; finished = false;
    }
    public boolean computeScrollOffset() {
        if (finished) return false;
        currentX = finalX; currentY = finalY; finished = true; return true;
    }
    public void abortAnimation() {
        currentX = finalX; currentY = finalY; finished = true;
    }
    public void forceFinished(boolean value) { finished = value; }
    public boolean isFinished() { return finished; }
    public int getCurrX() { return currentX; }
    public int getCurrY() { return currentY; }
    public int getFinalX() { return finalX; }
    public int getFinalY() { return finalY; }
    public float getCurrVelocity() { return 0f; }
    public boolean springBack(int startX, int startY, int minX, int maxX,
            int minY, int maxY) {
        finalX = Math.max(minX, Math.min(maxX, startX));
        finalY = Math.max(minY, Math.min(maxY, startY));
        finished = finalX == startX && finalY == startY;
        return !finished;
    }
    public void fling(int startX, int startY, int velocityX, int velocityY,
            int minX, int maxX, int minY, int maxY, int overX, int overY) {
        finalX = Math.max(minX, Math.min(maxX, startX + velocityX / 4));
        finalY = Math.max(minY, Math.min(maxY, startY + velocityY / 4));
        currentX = startX; currentY = startY; finished = false;
    }
}
