package android.content.res;

import android.graphics.drawable.Drawable;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;
import java.util.List;
import java.util.ArrayList;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import android.util.DisplayMetrics;

public class Resources {
    private static final Resources SYSTEM = new Resources("");
    private static final int TYPE_STRING = 0x03;
    private static final int TYPE_FLOAT = 0x04;
    private static final int TYPE_DIMENSION = 0x05;
    private static final int TYPE_REFERENCE = 0x01;
    private static final int TYPE_FIRST_COLOR_INT = 0x1c;
    private static final int TYPE_LAST_COLOR_INT = 0x1f;
    private final Map<Integer, Value> values = new HashMap<Integer, Value>();
    private final Map<Integer, Integer> valueScores =
        new HashMap<Integer, Integer>();
    private final Map<Integer, List<Value>> arrays =
        new HashMap<Integer, List<Value>>();
    private final Map<Integer, Map<Integer, Value>> bags =
        new HashMap<Integer, Map<Integer, Value>>();
    private final String apkPath;
    private final DisplayMetrics displayMetrics = new DisplayMetrics();
    private final Configuration configuration = new Configuration();

    public Resources() {
        this(System.getProperty("muplar.apk.resource.path", ""));
    }

    private Resources(String path) {
        apkPath = path;
        configuration.densityDpi = displayMetrics.densityDpi;
        configuration.screenWidthDp = Math.round(
            displayMetrics.widthPixels / displayMetrics.density);
        configuration.screenHeightDp = Math.round(
            displayMetrics.heightPixels / displayMetrics.density);
        configuration.smallestScreenWidthDp = Math.min(
            configuration.screenWidthDp, configuration.screenHeightDp);
        configuration.orientation = configuration.screenWidthDp >
            configuration.screenHeightDp ? Configuration.ORIENTATION_LANDSCAPE
            : Configuration.ORIENTATION_PORTRAIT;
        if (!apkPath.isEmpty()) loadTable();
    }

    public static Resources getSystem() { return SYSTEM; }

    public DisplayMetrics getDisplayMetrics() { return displayMetrics; }
    public Configuration getConfiguration() { return configuration; }
    public Theme newTheme() { return new Theme(); }
    public void updateConfiguration(Configuration updated,
                                    DisplayMetrics metrics) {
        if (updated != null) {
            configuration.orientation = updated.orientation;
            configuration.locale = updated.locale;
            configuration.sdkVersion = updated.sdkVersion;
        }
        values.clear();
        valueScores.clear();
        arrays.clear();
        bags.clear();
        if (!apkPath.isEmpty()) loadTable();
    }

    public String getResourcePath(int id) {
        Value value = resolve(id);
        if (value == null || value.type != TYPE_STRING || value.text == null)
            throw new NotFoundException(id);
        return value.text;
    }

    public int getResourceInt(int id) {
        Value value = resolve(id);
        if (value == null) throw new NotFoundException(id);
        return value.data;
    }
    public boolean getBoolean(int id) {
        Value value = resolve(id);
        if (value == null && (apkPath.isEmpty() || (id >>> 24) == 1)) return true;
        return getResourceInt(id) != 0;
    }
    public int getInteger(int id) {
        Value value = resolve(id);
        if (value == null && (apkPath.isEmpty() || (id >>> 24) == 1)) return 0;
        return getResourceInt(id);
    }
    public float getFloat(int id) {
        Value value = resolve(id);
        if (value == null) return 1.0f;
        return value.type == TYPE_FLOAT ? Float.intBitsToFloat(value.data)
            : value.data;
    }
    public int[] getIntArray(int id) {
        List<Value> array = arrays.get(id);
        if (array == null) {
            if (apkPath.isEmpty()) return new int[0];
            throw new NotFoundException(id);
        }
        int[] result = new int[array.size()];
        for (int i = 0; i < result.length; i++) result[i] = array.get(i).data;
        return result;
    }
    public String[] getStringArray(int id) {
        List<Value> array = arrays.get(id);
        if (array == null) return new String[0];
        String[] result = new String[array.size()];
        for (int i = 0; i < result.length; i++) {
            Value value = array.get(i);
            result[i] = value.type == TYPE_REFERENCE ? getString(value.data)
                : value.text != null ? value.text : Integer.toString(value.data);
        }
        return result;
    }
    public int getIdentifier(String name, String type, String packageName) {
        if (name == null || name.isEmpty()) return 0;
        if (apkPath.isEmpty() && "android".equals(packageName))
            return 0x01000000 | (name + ":" + type).hashCode() & 0x00ffffff;
        return 0;
    }
    public int getDimensionPixelSize(int id) { return Math.round(getDimension(id)); }
    public int getDimensionPixelOffset(int id) { return (int)getDimension(id); }

    public byte[] readResourceFile(int id) {
        String path = getResourcePath(id);
        try (ZipFile apk = new ZipFile(apkPath)) {
            ZipEntry entry = apk.getEntry(path);
            if (entry == null) throw new NotFoundException(id);
            return readAll(apk.getInputStream(entry));
        } catch (NotFoundException error) {
            throw error;
        } catch (Exception error) {
            throw new NotFoundException(path, error);
        }
    }
    public XmlResourceParser getXml(int id) {
        return new BinaryXmlResourceParser(readResourceFile(id));
    }
    public TypedArray obtainTypedArray(int id) {
        List<Value> array = arrays.get(id);
        if (array == null) throw new NotFoundException(id);
        int[] data = new int[array.size()];
        int[] types = new int[array.size()];
        for (int i = 0; i < array.size(); i++) {
            data[i] = array.get(i).data;
            types[i] = array.get(i).type;
        }
        return new TypedArray(data, types, displayMetrics);
    }
    public TypedArray obtainStyledAttributes(int styleId, int[] attributes) {
        Map<Integer, Value> bag = bags.get(styleId);
        int[] data = new int[attributes.length];
        int[] types = new int[attributes.length];
        boolean[] present = new boolean[attributes.length];
        if (bag != null) {
            for (int i = 0; i < attributes.length; i++) {
                Value value = bag.get(attributes[i]);
                if (value != null) {
                    data[i] = value.data;
                    types[i] = value.type;
                    present[i] = true;
                }
            }
        }
        return new TypedArray(data, types, present, displayMetrics);
    }

    public String getString(int id) {
        Value value = resolve(id);
        if (value == null && (id >>> 24) == 1)
            return "M50,0 A50,50 0 1,1 49.999,0 Z";
        if (value == null || value.type != TYPE_STRING)
            throw new NotFoundException(id);
        return value.text;
    }

    public int getColor(int id) {
        Value value = resolve(id);
        if (value == null || value.type < TYPE_FIRST_COLOR_INT ||
            value.type > TYPE_LAST_COLOR_INT) throw new NotFoundException(id);
        return value.data;
    }

    public float getDimension(int id) {
        Value value = resolve(id);
        if (value == null || value.type != TYPE_DIMENSION)
            throw new NotFoundException(id);
        int radix = (value.data >> 4) & 3;
        float[] factors = { 1.0f / 256.0f, 1.0f / 32768.0f,
            1.0f / 8388608.0f, 1.0f / 2147483648.0f };
        return (value.data & 0xffffff00) * factors[radix];
    }

    public Drawable getDrawable(int id) {
        Value value = resolve(id);
        if (value == null || value.type != TYPE_STRING || value.text == null)
            throw new NotFoundException(id);
        String lower = value.text.toLowerCase();
        if (!(lower.endsWith(".png") || lower.endsWith(".jpg") ||
              lower.endsWith(".jpeg") || lower.endsWith(".gif"))) {
            throw new NotFoundException(id);
        }
        return new Drawable(extract(value.text));
    }

    private Value resolve(int id) {
        Value value = values.get(id);
        int depth = 0;
        while (value != null && value.type == TYPE_REFERENCE && depth++ < 16)
            value = values.get(value.data);
        return value;
    }

    private void loadTable() {
        try (ZipFile apk = new ZipFile(apkPath)) {
            ZipEntry entry = apk.getEntry("resources.arsc");
            if (entry == null) return;
            byte[] data = readAll(apk.getInputStream(entry));
            if (u16(data, 0) != 0x0002) return;
            int tableEnd = Math.min(data.length, u32(data, 4));
            int offset = u16(data, 2);
            String[] globalStrings = new String[0];
            while (offset + 8 <= tableEnd) {
                int type = u16(data, offset);
                int headerSize = u16(data, offset + 2);
                int chunkSize = u32(data, offset + 4);
                if (chunkSize < headerSize || offset + chunkSize > tableEnd) break;
                if (type == 0x0001 && globalStrings.length == 0) {
                    globalStrings = stringPool(data, offset);
                } else if (type == 0x0200 && headerSize >= 284) {
                    readPackage(data, offset, chunkSize, headerSize,
                        u32(data, offset + 8), globalStrings);
                }
                offset += chunkSize;
            }
        } catch (Exception error) {
            System.err.println("[Resources] table load failed: " + error);
        }
    }

    private void readPackage(byte[] data, int packageOffset, int packageSize,
                             int packageHeader, int packageId,
                             String[] globalStrings) {
        int typeIdOffset = packageHeader >= 288
            ? u32(data, packageOffset + 284) : 0;
        int child = packageOffset + packageHeader;
        int end = packageOffset + packageSize;
        while (child + 20 <= end) {
            int type = u16(data, child);
            int header = u16(data, child + 2);
            int size = u32(data, child + 4);
            if (size < header || child + size > end) break;
            if (type == 0x0201) {
                int typeId = data[child + 8] & 0xff;
                int flags = data[child + 9] & 0xff;
                int count = u32(data, child + 12);
                int entriesStart = u32(data, child + 16);
                int configSize = u32(data, child + 20);
                int sdkVersion = configSize >= 28 ? u16(data, child + 44) : 0;
                int orientation = configSize >= 16 ? data[child + 32] & 0xff : 0;
                int density = configSize >= 16 ? u16(data, child + 34) : 0;
                String language = configSize >= 12 && data[child + 28] != 0
                    ? new String(new byte[]{data[child + 28], data[child + 29]},
                        StandardCharsets.US_ASCII) : "";
                if (sdkVersion > configuration.sdkVersion ||
                    (orientation != 0 && orientation != configuration.orientation) ||
                    (!language.isEmpty() && !language.equals(
                        configuration.locale.getLanguage()))) {
                    child += size;
                    continue;
                }
                int score = sdkVersion * 1000;
                if (!language.isEmpty()) score += 100;
                if (orientation != 0) score += 20;
                if (density != 0) {
                    score += Math.max(0, 10 - Math.abs(
                        density - displayMetrics.densityDpi) / 40);
                }
                for (int i = 0; i < count; ++i) {
                    int entryIndex = i;
                    int entryOffset;
                    if ((flags & 1) != 0) {
                        int sparse = child + header + i * 4;
                        if (sparse + 4 > child + size) break;
                        entryIndex = u16(data, sparse);
                        entryOffset = u16(data, sparse + 2) * 4;
                    } else if ((flags & 2) != 0) {
                        int position = child + header + i * 2;
                        if (position + 2 > child + size) break;
                        int compactOffset = u16(data, position);
                        if (compactOffset == 0xffff) continue;
                        entryOffset = compactOffset * 4;
                    } else {
                        int position = child + header + i * 4;
                        if (position + 4 > child + size) break;
                        entryOffset = u32(data, position);
                    }
                    if (entryOffset == -1) continue;
                    int entry = child + entriesStart + entryOffset;
                    if (entry + 8 > child + size) continue;
                    int entryFlags = u16(data, entry + 2);
                    int effectiveTypeId = typeId + typeIdOffset;
                    int id = (packageId << 24) |
                        (effectiveTypeId << 16) | entryIndex;
                    if ((entryFlags & 1) != 0) {
                        int entrySize = u16(data, entry);
                        int mapCount = entry + 16 <= child + size
                            ? u32(data, entry + 12) : 0;
                        List<Value> array = new ArrayList<Value>();
                        Map<Integer, Value> bag = new HashMap<Integer, Value>();
                        int map = entry + entrySize;
                        for (int item = 0; item < mapCount &&
                                map + item * 12 + 12 <= child + size; item++) {
                            int value = map + item * 12 + 4;
                            int mapName = u32(data, map + item * 12);
                            int mapType = data[value + 3] & 0xff;
                            int mapData = u32(data, value + 4);
                            String mapText = mapType == TYPE_STRING && mapData >= 0 &&
                                mapData < globalStrings.length ? globalStrings[mapData] : null;
                            Value mapValue = new Value(mapType, mapData, mapText);
                            array.add(mapValue);
                            bag.put(mapName, mapValue);
                        }
                        arrays.put(id, array);
                        bags.put(id, bag);
                        continue;
                    }
                    int valueType;
                    int valueData;
                    if ((entryFlags & 8) != 0) {
                        valueType = (entryFlags >> 8) & 0xff;
                        valueData = u32(data, entry + 4);
                    } else {
                        int value = entry + u16(data, entry);
                        if (value + 8 > child + size) continue;
                        valueType = data[value + 3] & 0xff;
                        valueData = u32(data, value + 4);
                    }
                    String text = valueType == TYPE_STRING &&
                        valueData >= 0 && valueData < globalStrings.length
                        ? globalStrings[valueData] : null;
                    Integer selectedScore = valueScores.get(id);
                    if (selectedScore == null || score >= selectedScore) {
                        values.put(id, new Value(valueType, valueData, text));
                        valueScores.put(id, score);
                    }
                }
            }
            child += size;
        }
    }

    private String extract(String entryName) {
        try (ZipFile apk = new ZipFile(apkPath)) {
            ZipEntry entry = apk.getEntry(entryName);
            if (entry == null) throw new NotFoundException(0);
            File base = new File(System.getProperty("muplar.prefix.state.dir",
                System.getProperty("java.io.tmpdir")), "resources");
            if (!base.isDirectory() && !base.mkdirs())
                throw new IllegalStateException("cannot create " + base);
            File output = new File(base,
                Integer.toHexString(entryName.hashCode()) + "-" +
                new File(entryName).getName());
            try (InputStream input = apk.getInputStream(entry);
                 FileOutputStream file = new FileOutputStream(output)) {
                byte[] buffer = new byte[8192];
                int read;
                while ((read = input.read(buffer)) >= 0)
                    file.write(buffer, 0, read);
            }
            return output.getAbsolutePath();
        } catch (Exception error) {
            throw new NotFoundException(entryName, error);
        }
    }

    private static String[] stringPool(byte[] data, int offset) {
        if (offset < 0 || offset + 28 > data.length ||
            u16(data, offset) != 0x0001) return new String[0];
        int header = u16(data, offset + 2);
        int count = u32(data, offset + 8);
        long chunkSize = u32long(data, offset + 4);
        boolean utf8 = (u32(data, offset + 16) & 0x100) != 0;
        long stringsStart = u32long(data, offset + 20);
        long chunkEnd = (long) offset + chunkSize;
        if (header < 28 || count < 0 || count > 1000000 ||
            chunkEnd > data.length ||
            (long) offset + header + (long) count * 4 > chunkEnd ||
            stringsStart < header || stringsStart >= chunkSize) {
            return new String[0];
        }
        String[] result = new String[count];
        for (int i = 0; i < count; ++i) {
            long relative = u32long(data, offset + header + i * 4);
            long absolute = (long) offset + stringsStart + relative;
            if (absolute < 0 || absolute >= chunkEnd) continue;
            int position = (int) absolute;
            if (utf8) {
                int[] first = length8(data, position);
                if (first == null || first[1] >= chunkEnd) continue;
                int[] bytes = length8(data, first[1]);
                if (bytes == null || bytes[0] < 0 ||
                    (long) bytes[1] + bytes[0] > chunkEnd) continue;
                result[i] = new String(data, bytes[1], bytes[0],
                    StandardCharsets.UTF_8);
            } else {
                int[] length = length16(data, position);
                if (length == null || length[0] < 0 ||
                    (long) length[1] + (long) length[0] * 2 > chunkEnd) continue;
                result[i] = new String(data, length[1], length[0] * 2,
                    StandardCharsets.UTF_16LE);
            }
        }
        return result;
    }

    private static int[] length8(byte[] data, int position) {
        if (position < 0 || position >= data.length) return null;
        int first = data[position++] & 0xff;
        if ((first & 0x80) == 0) return new int[]{first, position};
        if (position >= data.length) return null;
        return new int[]{((first & 0x7f) << 8) | (data[position++] & 0xff),
            position};
    }

    private static int[] length16(byte[] data, int position) {
        if (position < 0 || position + 2 > data.length) return null;
        int first = u16(data, position);
        position += 2;
        if ((first & 0x8000) == 0) return new int[]{first, position};
        if (position + 2 > data.length) return null;
        int second = u16(data, position);
        return new int[]{((first & 0x7fff) << 16) | second, position + 2};
    }

    private static int u16(byte[] data, int offset) {
        return (data[offset] & 0xff) | ((data[offset + 1] & 0xff) << 8);
    }

    private static int u32(byte[] data, int offset) {
        return (data[offset] & 0xff) | ((data[offset + 1] & 0xff) << 8) |
            ((data[offset + 2] & 0xff) << 16) | (data[offset + 3] << 24);
    }

    private static long u32long(byte[] data, int offset) {
        return u32(data, offset) & 0xffffffffL;
    }

    private static byte[] readAll(InputStream input) throws Exception {
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        int read;
        while ((read = input.read(buffer)) >= 0) output.write(buffer, 0, read);
        return output.toByteArray();
    }

    private static final class Value {
        final int type;
        final int data;
        final String text;
        Value(int type, int data, String text) {
            this.type = type; this.data = data; this.text = text;
        }
    }

    public static class NotFoundException extends RuntimeException {
        public NotFoundException(int id) {
            super("resource not found: 0x" + Integer.toHexString(id));
        }
        public NotFoundException(String value, Throwable cause) {
            super("resource not found: " + value, cause);
        }
    }

    public static class Theme {
        private int styleResourceId;
        public void applyStyle(int resourceId, boolean force) {
            if (force || styleResourceId == 0) styleResourceId = resourceId;
        }
        public int getAppliedStyleResourceId() { return styleResourceId; }
    }
}
