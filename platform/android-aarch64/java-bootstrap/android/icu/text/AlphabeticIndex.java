package android.icu.text;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

public class AlphabeticIndex<V> {
    public AlphabeticIndex(Locale locale) {}
    public AlphabeticIndex<V> addLabels(Locale... locales) { return this; }
    public AlphabeticIndex<V> setMaxLabelCount(int count) { return this; }
    public ImmutableIndex<V> buildImmutableIndex() { return new ImmutableIndex<>(); }

    public static final class ImmutableIndex<V> {
        private final List<Bucket<V>> buckets = new ArrayList<>();
        ImmutableIndex() {
            buckets.add(new Bucket<>("#"));
            for (char letter = 'A'; letter <= 'Z'; letter++)
                buckets.add(new Bucket<>(String.valueOf(letter)));
        }
        public int getBucketCount() { return buckets.size(); }
        public int getBucketIndex(CharSequence name) {
            if (name == null || name.length() == 0) return 0;
            char first = Character.toUpperCase(name.charAt(0));
            return first >= 'A' && first <= 'Z' ? first - 'A' + 1 : 0;
        }
        public Bucket<V> getBucket(int index) { return buckets.get(index); }
    }
    public static final class Bucket<V> {
        private final String label;
        Bucket(String label) { this.label = label; }
        public String getLabel() { return label; }
    }
}
