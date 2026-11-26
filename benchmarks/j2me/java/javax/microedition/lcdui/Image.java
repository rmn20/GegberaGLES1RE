package javax.microedition.lcdui;

import java.awt.image.BufferedImage;
import java.io.*;
import javax.imageio.ImageIO;

public final class Image {
    final BufferedImage image;
    private final boolean mutable;
    private Image(BufferedImage image, boolean mutable) {
        if (image == null)
            throw new IllegalArgumentException("Unsupported image");
        this.image = image;
        this.mutable = mutable;
    }

    public static Image createImage(String name) throws IOException {
        try (InputStream s = Image.class.getResourceAsStream(name)) {
            if (s == null)
                throw new IOException(name);
            return createImage(s);
        }
    }

    public static Image createImage(InputStream s) throws IOException {
        return new Image(ImageIO.read(s), false);
    }

    public static Image createImage(byte[] b, int off, int len) {
        try {
            return createImage(new ByteArrayInputStream(b, off, len));
        } catch (IOException e) {
            throw new IllegalArgumentException(e);
        }
    }

    public static Image createImage(int w, int h) {
        return new Image(new BufferedImage(w, h, BufferedImage.TYPE_INT_ARGB), true);
    }

    public static Image createImage(Image src) {
        return createImage(src, 0, 0, src.getWidth(), src.getHeight(), 0);
    }

    public static Image createImage(Image src, int x, int y, int w, int h, int transform) {
        if (transform != 0)
            throw new UnsupportedOperationException("image transform " + transform);
        int[] p = new int[w * h];
        src.getRGB(p, 0, w, x, y, w, h);
        return createRGBImage(p, w, h, true);
    }

    public static Image createRGBImage(int[] rgb, int w, int h, boolean alpha) {
        Image out = createImage(w, h);
        int[] p = rgb.clone();
        if (!alpha)
            for (int i = 0; i < p.length; i++) p[i] |= 0xff000000;
        out.image.setRGB(0, 0, w, h, p, 0, w);
        return new Image(out.image, false);
    }

    public int getWidth() {
        return image.getWidth();
    }

    public int getHeight() {
        return image.getHeight();
    }

    public boolean isMutable() {
        return mutable;
    }

    public void getRGB(int[] out, int off, int stride, int x, int y, int w, int h) {
        image.getRGB(x, y, w, h, out, off, stride);
    }

    public Graphics getGraphics() {
        if (!mutable)
            throw new IllegalStateException("immutable image");
        return new Graphics(this);
    }
}
