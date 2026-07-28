package com.muplar.runtime;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;

/**
 * Talks to muplard directly over its Unix domain socket via a small native
 * AF_UNIX bridge, without spawning a host process. Mirrors the wire protocol
 * implemented by the C++ MuplardClient (see services/muplard_protocol.h): a
 * 24-byte MessageHeader (magic, version, opcode, payload_size, 4 bytes
 * padding, request_id) followed by the payload bytes, all little-endian.
 */
final class MuplarSocketClient implements AutoCloseable {
    static final int MAGIC = 0x4d555044; // "MUPD"
    static final int VERSION = 1;
    static final int REPLY_FLAG = 0x8000;
    private static final int HEADER_SIZE = 24;
    private static final int MAX_PAYLOAD_SIZE = 1024 * 1024;

    static final class Frame {
        final int opcode;
        final String payload;

        Frame(int opcode, String payload) {
            this.opcode = opcode;
            this.payload = payload;
        }
    }

    private final int fd;

    private MuplarSocketClient(int fd) {
        this.fd = fd;
    }

    static MuplarSocketClient connect(String socketPath) {
        if (socketPath == null || socketPath.isEmpty())
            return null;
        int fd = nativeConnect(socketPath);
        return fd >= 0 ? new MuplarSocketClient(fd) : null;
    }

    /** One-shot request/reply against a fresh connection. Returns null on any failure. */
    static String request(String socketPath, int opcode, String payload) {
        try (MuplarSocketClient client = connect(socketPath)) {
            if (client == null || !client.sendFrame(opcode, payload))
                return null;
            Frame reply = client.readFrame();
            return (reply != null && reply.opcode == (opcode | REPLY_FLAG))
                ? reply.payload
                : null;
        }
    }

    boolean sendFrame(int opcode, String payload) {
        byte[] payloadBytes = payload == null
            ? new byte[0]
            : payload.getBytes(StandardCharsets.UTF_8);
        ByteBuffer header = ByteBuffer.allocate(HEADER_SIZE)
            .order(ByteOrder.LITTLE_ENDIAN);
        header.putInt(MAGIC);
        header.putShort((short) VERSION);
        header.putShort((short) opcode);
        header.putInt(payloadBytes.length);
        header.putInt(0); // 4 bytes padding before the 8-byte-aligned request_id
        header.putLong(1L);
        return writeAll(header.array()) && writeAll(payloadBytes);
    }

    /**
     * Reads one frame (header + payload). Returns null on a protocol
     * mismatch or a closed connection. Used both for a single request's
     * reply and, in a loop, for a subscription stream's pushed frames.
     */
    Frame readFrame() {
        byte[] headerBytes = readAll(HEADER_SIZE);
        if (headerBytes == null)
            return null;
        ByteBuffer header = ByteBuffer.wrap(headerBytes)
            .order(ByteOrder.LITTLE_ENDIAN);
        int magic = header.getInt();
        int version = header.getShort() & 0xFFFF;
        int opcode = header.getShort() & 0xFFFF;
        int payloadSize = header.getInt();
        if (magic != MAGIC || version != VERSION || payloadSize < 0 ||
            payloadSize > MAX_PAYLOAD_SIZE)
            return null;
        String payload = "";
        if (payloadSize > 0) {
            byte[] payloadBytes = readAll(payloadSize);
            if (payloadBytes == null)
                return null;
            payload = new String(payloadBytes, StandardCharsets.UTF_8);
        }
        return new Frame(opcode, payload);
    }

    private boolean writeAll(byte[] data) {
        int offset = 0;
        while (offset < data.length) {
            int written = nativeWrite(fd, data, offset, data.length - offset);
            if (written <= 0)
                return false;
            offset += written;
        }
        return true;
    }

    private byte[] readAll(int length) {
        byte[] buffer = new byte[length];
        int offset = 0;
        while (offset < length) {
            int count = nativeRead(fd, buffer, offset, length - offset);
            if (count <= 0)
                return null;
            offset += count;
        }
        return buffer;
    }

    @Override
    public void close() {
        nativeClose(fd);
    }

    private static native int nativeConnect(String path);
    private static native int nativeWrite(int fd, byte[] data, int offset, int length);
    private static native int nativeRead(int fd, byte[] buffer, int offset, int length);
    private static native void nativeClose(int fd);
}
