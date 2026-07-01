package android.content.res;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import org.xmlpull.v1.XmlPullParserException;

final class BinaryXmlResourceParser implements XmlResourceParser {
    private static final int START_ELEMENT = 0x0102;
    private static final int END_ELEMENT = 0x0103;
    private final List<Event> events = new ArrayList<>();
    private int position = -1;
    BinaryXmlResourceParser(byte[] xml) {
        parse(xml);
    }
    public int getEventType() { return position < 0 ? START_DOCUMENT
        : position >= events.size() ? END_DOCUMENT : events.get(position).type; }
    public int next() throws XmlPullParserException, IOException {
        if (position < events.size()) position++;
        return getEventType();
    }
    public int nextTag() throws XmlPullParserException, IOException {
        int event;
        do { event = next(); } while (event == TEXT);
        if (event != START_TAG && event != END_TAG)
            throw new XmlPullParserException("expected start or end tag");
        return event;
    }
    public String getName() { return current() == null ? null : current().name; }
    public String getText() { return null; }
    public int getDepth() { return current() == null ? 0 : current().depth; }
    public int getAttributeCount() {
        Event event = current(); return event == null ? -1 : event.attributes.size();
    }
    public String getAttributeName(int index) {
        return current().attributes.get(index).name;
    }
    public String getAttributeValue(int index) {
        Attribute attribute = current().attributes.get(index);
        return attribute.raw != null ? attribute.raw : Integer.toString(attribute.data);
    }
    public String getAttributeValue(String namespace, String name) {
        Attribute attribute = attribute(name);
        return attribute == null ? null
            : attribute.raw != null ? attribute.raw : Integer.toString(attribute.data);
    }
    public int getAttributeResourceValue(int index, int fallback) {
        Attribute attribute = current().attributes.get(index);
        return attribute.type == 1 ? attribute.data : fallback;
    }
    public int getAttributeIntValue(int index, int fallback) {
        return current().attributes.get(index).data;
    }
    public float getAttributeFloatValue(int index, float fallback) {
        Attribute attribute = current().attributes.get(index);
        return attribute.type == 4 ? Float.intBitsToFloat(attribute.data) : attribute.data;
    }
    public boolean getAttributeBooleanValue(int index, boolean fallback) {
        return current().attributes.get(index).data != 0;
    }
    public int getAttributeNameResource(int index) {
        return current().attributes.get(index).nameResource;
    }
    public int getAttributeValueType(int index) {
        return current().attributes.get(index).type;
    }
    public int getAttributeValueData(int index) {
        return current().attributes.get(index).data;
    }
    public void close() { position = events.size(); }
    private Event current() {
        return position < 0 || position >= events.size() ? null : events.get(position);
    }
    private Attribute attribute(String name) {
        Event event = current();
        if (event == null) return null;
        for (Attribute attribute : event.attributes)
            if (attribute.name.equals(name)) return attribute;
        return null;
    }
    private void parse(byte[] xml) {
        if (xml.length < 8 || u16(xml, 0) != 3)
            throw new IllegalArgumentException("compiled Android XML required");
        String[] strings = new String[0];
        int[] resourceIds = new int[0];
        int depth = 0;
        for (int offset = 8; offset + 8 <= xml.length;) {
            int type = u16(xml, offset);
            int header = u16(xml, offset + 2);
            int size = u32(xml, offset + 4);
            if (size < header || offset + size > xml.length) break;
            if (type == 1) strings = stringPool(xml, offset);
            else if (type == 0x0180) {
                resourceIds = new int[(size - header) / 4];
                for (int i = 0; i < resourceIds.length; i++)
                    resourceIds[i] = u32(xml, offset + header + i * 4);
            } else if (type == START_ELEMENT) {
                depth++;
                Event event = new Event(START_TAG, string(strings, u32(xml, offset + 20)), depth);
                int attributeStart = u16(xml, offset + 24);
                int attributeSize = u16(xml, offset + 26);
                int attributeCount = u16(xml, offset + 28);
                int attributes = offset + 16 + attributeStart;
                for (int i = 0; i < attributeCount; i++) {
                    int item = attributes + i * attributeSize;
                    int nameIndex = u32(xml, item + 4);
                    int rawIndex = u32(xml, item + 8);
                    Attribute attribute = new Attribute();
                    attribute.name = string(strings, nameIndex);
                    attribute.raw = string(strings, rawIndex);
                    attribute.type = xml[item + 15] & 0xff;
                    attribute.data = u32(xml, item + 16);
                    attribute.nameResource = nameIndex >= 0 && nameIndex < resourceIds.length
                        ? resourceIds[nameIndex] : 0;
                    event.attributes.add(attribute);
                }
                events.add(event);
            } else if (type == END_ELEMENT) {
                events.add(new Event(END_TAG, string(strings, u32(xml, offset + 20)), depth));
                depth--;
            }
            offset += size;
        }
    }
    private static String string(String[] strings, int index) {
        return index >= 0 && index < strings.length ? strings[index] : null;
    }
    private static String[] stringPool(byte[] data, int offset) {
        int header = u16(data, offset + 2), count = u32(data, offset + 8);
        boolean utf8 = (u32(data, offset + 16) & 0x100) != 0;
        int start = u32(data, offset + 20);
        String[] result = new String[count];
        for (int i = 0; i < count; i++) {
            int position = offset + start + u32(data, offset + header + i * 4);
            int[] length = utf8 ? length8(data, length8(data, position)[1])
                : length16(data, position);
            result[i] = new String(data, length[1], utf8 ? length[0] : length[0] * 2,
                utf8 ? StandardCharsets.UTF_8 : StandardCharsets.UTF_16LE);
        }
        return result;
    }
    private static int[] length8(byte[] data, int p) {
        int first = data[p++] & 255;
        return (first & 128) == 0 ? new int[]{first, p}
            : new int[]{((first & 127) << 8) | (data[p++] & 255), p};
    }
    private static int[] length16(byte[] data, int p) {
        int first = u16(data, p); p += 2;
        return (first & 0x8000) == 0 ? new int[]{first, p}
            : new int[]{((first & 0x7fff) << 16) | u16(data, p), p + 2};
    }
    private static int u16(byte[] d, int o) { return (d[o]&255)|((d[o+1]&255)<<8); }
    private static int u32(byte[] d, int o) {
        return (d[o]&255)|((d[o+1]&255)<<8)|((d[o+2]&255)<<16)|(d[o+3]<<24);
    }
    private static final class Event {
        final int type; final String name; final int depth;
        final List<Attribute> attributes = new ArrayList<>();
        Event(int type, String name, int depth) {
            this.type = type; this.name = name; this.depth = depth;
        }
    }
    private static final class Attribute {
        String name; String raw; int type; int data; int nameResource;
    }
}
