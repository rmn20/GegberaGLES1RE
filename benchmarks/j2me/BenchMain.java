import javax.microedition.lcdui.Graphics;

public final class BenchMain {
    private static native void frameEnd();
    public static void main(String[] args) throws Exception {
        System.load(System.getProperty("bench.library"));
        Object canvas = JarWorkloads.create(args[0], Integer.parseInt(args[1]));
        Graphics graphics = new Graphics(
            Integer.getInteger("bench.width", 320), Integer.getInteger("bench.height", 240));
        String[] times = args[2].split(",");
        int warmup = Integer.parseInt(args[3]), frame = 0;
        for (int i = 0; i < warmup; i++) {
            JarWorkloads.paint(canvas, graphics, 0);
            frameEnd();
            frame++;
        }
        for (String time : times) {
            JarWorkloads.paint(canvas, graphics, Integer.parseInt(time));
            frameEnd();
            graphics.getBitmap().save(args[4] + "/host_" + frame + ".png");
            String state = JarWorkloads.state(canvas);
            if (!state.isEmpty())
                System.out.println("frame=" + frame + " " + state);
            frame++;
        }
        System.out.println("Captured " + times.length + " GLES frame(s)");
    }
}
