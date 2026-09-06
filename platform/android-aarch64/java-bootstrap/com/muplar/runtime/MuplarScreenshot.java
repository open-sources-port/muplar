package com.muplar.runtime;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.view.View;
import android.view.ViewGroup;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.zip.CRC32;
import java.util.zip.Deflater;

final class MuplarScreenshot {
    private static boolean captured;

    private MuplarScreenshot() {
    }

    static void captureIfRequested(View root) {
        if (root == null) {
            return;
        }
        if (captured) {
            return;
        }
        String path = System.getenv("MUPLAR_LAUNCHER3_SCREENSHOT");
        if (path == null || path.isEmpty()) {
            return;
        }
        captured = true;
        int width = root.getWidth();
        int height = root.getHeight();
        if (width <= 0 || height <= 0) {
            width = 1080;
            height = 1920;
            try {
                int wSpec = View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY);
                int hSpec = View.MeasureSpec.makeMeasureSpec(height, View.MeasureSpec.EXACTLY);
                root.measure(wSpec, hSpec);
                root.layout(0, 0, width, height);
            } catch (Throwable t) {
                System.err.println("[Muplar/Window] measure/layout fallback failed: " + t);
            }
        }
        width = Math.max(1, root.getWidth());
        height = Math.max(1, root.getHeight());
        dumpViewTree(root, 0);
        try {
            File file = new File(path);
            File parent = file.getParentFile();
            if (parent != null) {
                parent.mkdirs();
            }
            if (captureWithAndroidCanvas(root, file, width, height)) {
                System.out.println("[Muplar/Window] screenshot written: " + path);
                return;
            }
            writeFallbackPng(file, width, height);
            System.out.println("[Muplar/Window] fallback screenshot written: " + path);
        } catch (Throwable t) {
            System.err.println("[Muplar/Window] screenshot failed: " + t);
        }
    }

    private static void dumpViewTree(View view, int depth) {
        if (view == null || depth > 8) {
            return;
        }
        StringBuilder line = new StringBuilder("[Muplar/Window] view ");
        for (int i = 0; i < depth; i++) {
            line.append("  ");
        }
        line.append(view.getClass().getName())
            .append(" vis=").append(view.getVisibility())
            .append(" alpha=").append(view.getAlpha())
            .append(" bounds=").append(view.getLeft()).append(',')
            .append(view.getTop()).append('-')
            .append(view.getRight()).append(',')
            .append(view.getBottom());
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            line.append(" children=").append(group.getChildCount());
            System.out.println(line.toString());
            int count = Math.min(8, group.getChildCount());
            for (int i = 0; i < count; i++) {
                dumpViewTree(group.getChildAt(i), depth + 1);
            }
        } else {
            System.out.println(line.toString());
        }
    }

    private static boolean captureWithAndroidCanvas(View root,
                                                    File file,
                                                    int width,
                                                    int height) {
        Bitmap bitmap = null;
        try {
            bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
            Canvas canvas = new Canvas(bitmap);
            canvas.drawColor(Color.rgb(238, 238, 238));
            root.draw(canvas);
            FileOutputStream out = new FileOutputStream(file);
            try {
                return bitmap.compress(Bitmap.CompressFormat.PNG, 100, out);
            } finally {
                out.close();
            }
        } catch (Throwable t) {
            System.err.println("[Muplar/Window] Android screenshot path failed: " + t);
            return false;
        } finally {
            if (bitmap != null) {
                try {
                    bitmap.recycle();
                } catch (Throwable ignored) {
                }
            }
        }
    }

    private static void writeFallbackPng(File file, int width, int height)
        throws IOException {
        ByteArrayOutputStream raw = new ByteArrayOutputStream();
        for (int y = 0; y < height; y++) {
            raw.write(0);
            for (int x = 0; x < width; x++) {
                int color = fallbackColor(x, y, width, height);
                raw.write((color >> 16) & 0xff);
                raw.write((color >> 8) & 0xff);
                raw.write(color & 0xff);
            }
        }

        ByteArrayOutputStream png = new ByteArrayOutputStream();
        png.write(new byte[] {
            (byte) 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'
        });

        ByteArrayOutputStream ihdr = new ByteArrayOutputStream();
        writeU32(ihdr, width);
        writeU32(ihdr, height);
        ihdr.write(8);
        ihdr.write(2);
        ihdr.write(0);
        ihdr.write(0);
        ihdr.write(0);
        writeChunk(png, "IHDR", ihdr.toByteArray());
        writeChunk(png, "IDAT", deflate(raw.toByteArray()));
        writeChunk(png, "IEND", new byte[0]);

        FileOutputStream out = new FileOutputStream(file);
        try {
            out.write(png.toByteArray());
        } finally {
            out.close();
        }
    }

    private static int fallbackColor(int x, int y, int width, int height) {
        if (y < Math.max(1, height / 18)) {
            return 0x263238;
        }
        if (y > height - Math.max(1, height / 14)) {
            return 0x101820;
        }
        int cell = ((x / Math.max(1, width / 6)) + (y / Math.max(1, height / 10))) & 1;
        if (cell == 0) {
            return 0xf1f3f4;
        }
        return 0xd7e3f4;
    }

    private static byte[] deflate(byte[] input) {
        Deflater deflater = new Deflater(Deflater.BEST_SPEED);
        deflater.setInput(input);
        deflater.finish();
        byte[] buffer = new byte[8192];
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        while (!deflater.finished()) {
            int count = deflater.deflate(buffer);
            out.write(buffer, 0, count);
        }
        deflater.end();
        return out.toByteArray();
    }

    private static void writeChunk(ByteArrayOutputStream out,
                                   String type,
                                   byte[] data) throws IOException {
        byte[] typeBytes = type.getBytes("US-ASCII");
        writeU32(out, data.length);
        out.write(typeBytes);
        out.write(data);
        CRC32 crc = new CRC32();
        crc.update(typeBytes);
        crc.update(data);
        writeU32(out, (int) crc.getValue());
    }

    private static void writeU32(ByteArrayOutputStream out, int value) {
        out.write((value >> 24) & 0xff);
        out.write((value >> 16) & 0xff);
        out.write((value >> 8) & 0xff);
        out.write(value & 0xff);
    }
}
