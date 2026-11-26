package android.graphics;

import java.nio.ByteBuffer;

public final class Bitmap {
    public final ByteBuffer pixels;
    private final int width, height;
    public Bitmap(int w, int h) {
        width = w;
        height = h;
        pixels = ByteBuffer.allocateDirect(w * h * 4);
    }

    public void save(String filename) throws java.io.IOException {
        java.awt.image.BufferedImage image = new java.awt.image.BufferedImage(
            width, height, java.awt.image.BufferedImage.TYPE_INT_RGB);
        int[] rgb = new int[width * height];
        for (int i = 0; i < rgb.length; i++)
            rgb[i] = ((pixels.get(i * 4) & 255) << 16) | ((pixels.get(i * 4 + 1) & 255) << 8)
                | (pixels.get(i * 4 + 2) & 255);
        image.setRGB(0, 0, width, height, rgb, 0, width);
        javax.imageio.ImageIO.write(image, "png", new java.io.File(filename));
    }

    public int getWidth() {
        return width;
    }

    public int getHeight() {
        return height;
    }
}
