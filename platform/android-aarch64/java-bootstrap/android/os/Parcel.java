package android.os;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

public final class Parcel {
    private static final int MAGIC = 0x4d425031;
    private static final int INTERFACE_TOKEN = 0;
    private static final int INT32 = 1;
    private static final int INT64 = 3;
    private static final int FLOAT = 5;
    private static final int DOUBLE = 6;
    private static final int BOOL = 7;
    private static final int STRONG_BINDER = 10;
    private static final int STATUS = 11;
    private static final int STRING = 12;

    private static final class Value {
        int kind;
        long number;
        String text = "";
        Value(int kind, long number, String text) {
            this.kind = kind;
            this.number = number;
            if (text != null) this.text = text;
        }
    }

    private final List<Value> values = new ArrayList<Value>();
    private int position;

    private Parcel() {}
    public static Parcel obtain() { return new Parcel(); }
    public void recycle() { values.clear(); position = 0; }
    public int dataPosition() { return position; }
    public void setDataPosition(int position) {
        if (position < 0 || position > values.size())
            throw new IllegalArgumentException("invalid parcel position");
        this.position = position;
    }
    public int dataSize() { return values.size(); }

    public void writeInterfaceToken(String descriptor) {
        values.add(new Value(INTERFACE_TOKEN, 0, descriptor));
    }
    public void enforceInterface(String descriptor) {
        Value value = read(INTERFACE_TOKEN);
        if (!descriptor.equals(value.text))
            throw new SecurityException("Binder interface mismatch");
    }
    public void writeInt(int value) { values.add(new Value(INT32, value, null)); }
    public int readInt() { return (int)read(INT32).number; }
    public void writeLong(long value) { values.add(new Value(INT64, value, null)); }
    public long readLong() { return read(INT64).number; }
    public void writeFloat(float value) {
        values.add(new Value(FLOAT, Float.floatToRawIntBits(value), null));
    }
    public float readFloat() {
        return Float.intBitsToFloat((int)read(FLOAT).number);
    }
    public void writeDouble(double value) {
        values.add(new Value(DOUBLE, Double.doubleToRawLongBits(value), null));
    }
    public double readDouble() {
        return Double.longBitsToDouble(read(DOUBLE).number);
    }
    public void writeBoolean(boolean value) {
        values.add(new Value(BOOL, value ? 1 : 0, null));
    }
    public boolean readBoolean() { return read(BOOL).number != 0; }
    public void writeString(String value) {
        values.add(new Value(STRING, value == null ? 0 : 1,
            value == null ? "" : value));
    }
    public String readString() {
        Value value = read(STRING);
        return value.number == 0 ? null : value.text;
    }
    public void writeStrongBinder(IBinder binder) {
        values.add(new Value(STRONG_BINDER, binder == null ? 0 : 1, null));
    }
    public IBinder readStrongBinder() {
        read(STRONG_BINDER);
        return null;
    }
    public void writeNoException() { values.add(new Value(STATUS, 0, null)); }
    public void readException() throws RemoteException {
        if (position < values.size() && values.get(position).kind == STATUS) {
            Value status = values.get(position++);
            if (status.number != 0) throw new RemoteException(status.text);
        }
    }

    private Value read(int expectedKind) {
        if (position >= values.size())
            throw new IllegalStateException("parcel exhausted");
        Value value = values.get(position++);
        if (value.kind != expectedKind)
            throw new IllegalStateException("unexpected parcel value " + value.kind);
        return value;
    }

    byte[] encode(int code, int flags) {
        try {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            DataOutputStream output = new DataOutputStream(bytes);
            writeIntLE(output, MAGIC);
            writeIntLE(output, code);
            writeIntLE(output, flags);
            writeIntLE(output, values.size());
            for (Value value : values) {
                writeIntLE(output, value.kind);
                writeLongLE(output, value.number);
                byte[] text = value.text.getBytes("UTF-8");
                writeIntLE(output, text.length);
                output.write(text);
                writeIntLE(output, 0);
                writeIntLE(output, 0);
            }
            return bytes.toByteArray();
        } catch (IOException impossible) {
            throw new IllegalStateException(impossible);
        }
    }

    void decode(byte[] encoded) throws RemoteException {
        try {
            DataInputStream input = new DataInputStream(
                new ByteArrayInputStream(encoded));
            if (readIntLE(input) != MAGIC) throw new IOException("bad parcel magic");
            readIntLE(input);
            readIntLE(input);
            int count = readIntLE(input);
            if (count < 0 || count > 4096) throw new IOException("bad value count");
            values.clear();
            position = 0;
            for (int index = 0; index < count; index++) {
                int kind = readIntLE(input);
                long number = readLongLE(input);
                int textSize = readIntLE(input);
                if (textSize < 0 || textSize > 1024 * 1024)
                    throw new IOException("bad text size");
                byte[] text = new byte[textSize];
                input.readFully(text);
                int elementCount = readIntLE(input);
                for (int element = 0; element < elementCount; element++)
                    readLongLE(input);
                int stringCount = readIntLE(input);
                for (int string = 0; string < stringCount; string++) {
                    int size = readIntLE(input);
                    if (size < 0 || size > 1024 * 1024)
                        throw new IOException("bad string size");
                    input.skipBytes(size);
                }
                if (kind != INTERFACE_TOKEN)
                    values.add(new Value(kind, number, new String(text, "UTF-8")));
            }
        } catch (IOException error) {
            throw new RemoteException("Invalid Binder reply: " + error.getMessage());
        }
    }

    private static void writeIntLE(DataOutputStream output, int value)
        throws IOException {
        output.writeInt(Integer.reverseBytes(value));
    }
    private static int readIntLE(DataInputStream input) throws IOException {
        return Integer.reverseBytes(input.readInt());
    }
    private static void writeLongLE(DataOutputStream output, long value)
        throws IOException {
        output.writeLong(Long.reverseBytes(value));
    }
    private static long readLongLE(DataInputStream input) throws IOException {
        return Long.reverseBytes(input.readLong());
    }
}
