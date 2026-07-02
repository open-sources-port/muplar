package android.content;

public class ClipData {
    public static class Item {
        private final CharSequence text;
        public Item(CharSequence text) { this.text = text; }
        public CharSequence getText() { return text; }
    }
    private final ClipDescription description;
    private final java.util.List<Item> items = new java.util.ArrayList<Item>();
    public ClipData(ClipDescription description, Item item) {
        this.description = description;
        if (item != null) items.add(item);
    }
    public static ClipData newPlainText(CharSequence label, CharSequence text) {
        return new ClipData(new ClipDescription(label, new String[]{"text/plain"}),
            new Item(text));
    }
    public ClipDescription getDescription() { return description; }
    public int getItemCount() { return items.size(); }
    public Item getItemAt(int index) { return items.get(index); }
    public void addItem(Item item) { if (item != null) items.add(item); }
}
