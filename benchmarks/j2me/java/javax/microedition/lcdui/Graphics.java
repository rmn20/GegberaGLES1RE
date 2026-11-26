package javax.microedition.lcdui;

import android.graphics.Bitmap;

/**
 * Tracks canvas clipping for M3G without rendering canvas 2D overlays. Image pixel,
 * blit, and primitive operations use BufferedImage; text drawing is omitted.
 */
public final class Graphics {
    public static final int HCENTER = 1, VCENTER = 2, LEFT = 4, RIGHT = 8, TOP = 16, BOTTOM = 32,
                            BASELINE = 64, SOLID = 0, DOTTED = 1;
    private final Bitmap bitmap;
    private final java.awt.Graphics2D imageGraphics;
    private int x, y, w, h, tx, ty, color;
    private Font font = Font.getDefaultFont();
    public Graphics(int width, int height) {
        bitmap = new Bitmap(width, height);
        w = width;
        h = height;
        imageGraphics = null;
    }

    Graphics(Image image) {
        bitmap = new Bitmap(image.getWidth(), image.getHeight());
        w = image.getWidth();
        h = image.getHeight();
        imageGraphics = image.image.createGraphics();
    }

    public Bitmap getBitmap() {
        return bitmap;
    }

    public int getClipX() {
        return x;
    }

    public int getClipY() {
        return y;
    }

    public int getClipWidth() {
        return w;
    }

    public int getClipHeight() {
        return h;
    }

    public int getTranslateX() {
        return tx;
    }

    public int getTranslateY() {
        return ty;
    }

    public void translate(int dx, int dy) {
        tx += dx;
        ty += dy;
        x -= dx;
        y -= dy;
        if (imageGraphics != null)
            imageGraphics.translate(dx, dy);
    }

    public void setClip(int a, int b, int c, int d) {
        x = a;
        y = b;
        w = c;
        h = d;
        if (imageGraphics != null)
            imageGraphics.setClip(a, b, c, d);
    }

    public void clipRect(int a, int b, int c, int d) {
        int right = Math.min(x + w, a + c), bottom = Math.min(y + h, b + d);
        x = Math.max(x, a);
        y = Math.max(y, b);
        w = Math.max(0, right - x);
        h = Math.max(0, bottom - y);
        if (imageGraphics != null)
            imageGraphics.clipRect(a, b, c, d);
    }

    public void setColor(int rgb) {
        color = rgb;
        if (imageGraphics != null)
            imageGraphics.setColor(new java.awt.Color(rgb));
    }

    public void setColor(int r, int g, int b) {
        setColor((r << 16) | (g << 8) | b);
    }

    public int getColor() {
        return color;
    }

    public void setGrayScale(int v) {
        setColor(v, v, v);
    }

    public void setFont(Font f) {
        font = f == null ? Font.getDefaultFont() : f;
    }

    public Font getFont() {
        return font;
    }

    public void setStrokeStyle(int s) {}

    public void fillRect(int x, int y, int w, int h) {
        if (imageGraphics != null)
            imageGraphics.fillRect(x, y, w, h);
    }

    public void drawRect(int x, int y, int w, int h) {
        if (imageGraphics != null)
            imageGraphics.drawRect(x, y, w, h);
    }

    public void drawLine(int x, int y, int x2, int y2) {
        if (imageGraphics != null)
            imageGraphics.drawLine(x, y, x2, y2);
    }

    public void fillTriangle(int x1, int y1, int x2, int y2, int x3, int y3) {
        if (imageGraphics != null)
            imageGraphics.fillPolygon(new int[] {x1, x2, x3}, new int[] {y1, y2, y3}, 3);
    }

    public void drawImage(Image img, int x, int y, int anchor) {
        if (imageGraphics != null) {
            if ((anchor & HCENTER) != 0)
                x -= img.getWidth() / 2;
            else if ((anchor & RIGHT) != 0)
                x -= img.getWidth();
            if ((anchor & VCENTER) != 0)
                y -= img.getHeight() / 2;
            else if ((anchor & BOTTOM) != 0)
                y -= img.getHeight();
            imageGraphics.drawImage(img.image, x, y, null);
        }
    }

    public void drawRegion(
        Image img, int sx, int sy, int w, int h, int transform, int x, int y, int anchor) {
        if (imageGraphics != null)
            drawImage(Image.createImage(img, sx, sy, w, h, transform), x, y, anchor);
    }

    public void drawRGB(
        int[] rgb, int offset, int stride, int x, int y, int w, int h, boolean alpha) {
        if (imageGraphics != null) {
            int[] p = new int[w * h];
            for (int row = 0; row < h; row++)
                System.arraycopy(rgb, offset + row * stride, p, row * w, w);
            drawImage(Image.createRGBImage(p, w, h, alpha), x, y, 0);
        }
    }

    public void drawString(String s, int x, int y, int anchor) {}

    public void drawSubstring(String s, int off, int len, int x, int y, int anchor) {}

    public void drawChar(char c, int x, int y, int anchor) {}

    public void drawChars(char[] c, int off, int len, int x, int y, int anchor) {}
}
