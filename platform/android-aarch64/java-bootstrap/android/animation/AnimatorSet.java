package android.animation;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.List;

public final class AnimatorSet extends Animator {
    private final ArrayList<Animator> children = new ArrayList<>();

    public void playTogether(Animator... items) {
        children.clear();
        children.addAll(Arrays.asList(items));
    }
    public void playTogether(Collection<Animator> items) {
        children.clear();
        children.addAll(items);
    }
    public void playSequentially(Animator... items) { playTogether(items); }
    public void playSequentially(List<Animator> items) { playTogether(items); }
    public Builder play(Animator animator) {
        if (!children.contains(animator)) children.add(animator);
        return new Builder(animator);
    }
    public ArrayList<Animator> getChildAnimations() { return new ArrayList<>(children); }
    @Override public AnimatorSet setDuration(long duration) {
        super.setDuration(duration);
        return this;
    }

    @Override protected void applyFinalValue() {
        for (Animator animator : children) animator.start();
    }

    public final class Builder {
        private final Animator current;
        Builder(Animator current) { this.current = current; }
        public Builder with(Animator animator) { add(animator); return this; }
        public Builder before(Animator animator) { add(animator); return this; }
        public Builder after(Animator animator) { add(animator); return this; }
        public Builder after(long delay) { return this; }
        private void add(Animator animator) {
            if (animator != null && !children.contains(animator)) children.add(animator);
        }
    }
}
