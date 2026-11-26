import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import javax.microedition.lcdui.Graphics;

/** Selects reproducible scenes while executing each JAR's original render code. */
public final class JarWorkloads {
    private JarWorkloads() {}

    public static Object create(String workload, int level) throws Exception {
        if ("custombench".equals(workload)) {
            Class<?> midletClass = Class.forName("custombench.Midlet");
            Object midlet = midletClass.getConstructor().newInstance();
            Object canvas = Class.forName("custombench.CustomBench")
                    .getConstructor(midletClass, int.class).newInstance(midlet, 1);
            Object world = field(canvas, "world").get(canvas);
            if (world == null) {
                throw new IllegalStateException("CustomBench could not load its M3G world");
            }
            int coloredMeshes = coloredMeshes(world);
            if (coloredMeshes == 0) {
                throw new IllegalStateException("CustomBench nearest requires vertex colors");
            }
            System.out.println("CustomBench mode=1 nearest vertex_colors=enabled colored_meshes="
                    + coloredMeshes);
            return canvas;
        }
        if ("vorkuta".equals(workload)) {
            Class<?> midletClass = Class.forName("code.Midlet");
            Constructor<?> constructor = Class.forName("code.Midlet$MyCanvas")
                    .getDeclaredConstructor(midletClass);
            constructor.setAccessible(true);
            return constructor.newInstance(midletClass.getConstructor().newInstance());
        }
        if ("l2d".equals(workload)) {
            if (level != 1 && level != 2 && level != 5) {
                throw new IllegalArgumentException("L2D comparison levels are 1, 2 and 5");
            }
            Class<?> mainClass = Class.forName("home.Main");
            Object main = mainClass.getConstructor().newInstance();
            mainClass.getMethod("setLanguage", String.class).invoke(main, "/english.txt");
            mainClass.getMethod("setSound", boolean.class).invoke(main, false);
            mainClass.getMethod("setDisplaySize", int.class).invoke(main, 100);
            Object canvas = Class.forName("com.GameScreen")
                    .getConstructor(mainClass, String.class, int.class, Object.class)
                    .newInstance(main, "/level" + level + ".txt", level, null);
            Object player = field(canvas, "player").get(canvas);
            Object scene = field(canvas, "scene").get(canvas);
            if (player == null || scene == null) {
                throw new IllegalStateException("L2D could not load level " + level);
            }
            Object character = player.getClass().getMethod("getCharacter").invoke(player);
            character.getClass().getMethod("rotY", int.class).invoke(character, 180);
            Object rotation = character.getClass().getMethod("getRotation").invoke(character);
            if (rotation.getClass().getField("y").getInt(rotation) != 8192) {
                throw new IllegalStateException("L2D camera is not turned 180 degrees");
            }
            Object renderer = scene.getClass().getMethod("getG3D").invoke(scene);
            System.out.println("L2D level=" + level + " yaw_degrees=180 display_size=100 viewport="
                    + renderer.getClass().getMethod("getWidth").invoke(renderer) + "x"
                    + renderer.getClass().getMethod("getHeight").invoke(renderer));
            return canvas;
        }
        throw new IllegalArgumentException("Unknown workload: " + workload);
    }

    public static void paint(Object canvas, Graphics graphics, int timeMillis) throws Exception {
        if ("custombench.CustomBench".equals(canvas.getClass().getName())) {
            if (timeMillis < 0 || timeMillis >= 30000) {
                throw new IllegalArgumentException("CustomBench animation time must be 0..29999 ms");
            }
            // The original paint method passes this elapsed time to World.animate.
            field(canvas, "runTime").setLong(canvas, timeMillis);
        }
        Method paint = canvas.getClass().getDeclaredMethod("paint", Graphics.class);
        paint.setAccessible(true);
        try {
            paint.invoke(canvas, graphics);
        } catch (InvocationTargetException error) {
            Throwable cause = error.getCause();
            if (cause instanceof Exception) {
                throw (Exception) cause;
            }
            if (cause instanceof Error) {
                throw (Error) cause;
            }
            throw error;
        }
    }

    public static String state(Object canvas) throws Exception {
        if (!"com.GameScreen".equals(canvas.getClass().getName())) {
            return "";
        }
        Object player = field(canvas, "player").get(canvas);
        Object character = player.getClass().getMethod("getCharacter").invoke(player);
        Object position = character.getClass().getMethod("getPosition").invoke(character);
        Object rotation = character.getClass().getMethod("getRotation").invoke(character);
        return "position=" + vector(position) + " rotation_turn16384=" + vector(rotation)
                + " on_floor=" + character.getClass().getMethod("isOnFloor").invoke(character);
    }

    private static String vector(Object vector) throws ReflectiveOperationException {
        return vector.getClass().getField("x").getInt(vector) + ","
                + vector.getClass().getField("y").getInt(vector) + ","
                + vector.getClass().getField("z").getInt(vector);
    }

    private static Field field(Object object, String name) throws NoSuchFieldException {
        Field field = object.getClass().getDeclaredField(name);
        field.setAccessible(true);
        return field;
    }

    private static int coloredMeshes(Object node) throws Exception {
        Class<?> mesh = Class.forName("javax.microedition.m3g.Mesh");
        Class<?> group = Class.forName("javax.microedition.m3g.Group");
        int count = 0;
        if (mesh.isInstance(node)) {
            Object vertices = mesh.getMethod("getVertexBuffer").invoke(node);
            if (vertices.getClass().getMethod("getColors").invoke(vertices) != null) {
                count++;
            }
        }
        if (group.isInstance(node)) {
            int children = (Integer) group.getMethod("getChildCount").invoke(node);
            Method child = group.getMethod("getChild", int.class);
            for (int i = 0; i < children; i++) {
                count += coloredMeshes(child.invoke(node, i));
            }
        }
        return count;
    }
}
