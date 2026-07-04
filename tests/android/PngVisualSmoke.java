import java.awt.image.BufferedImage;
import java.io.File;
import java.util.HashSet;
import java.util.Set;
import javax.imageio.ImageIO;

public final class PngVisualSmoke {
    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new AssertionError("PNG path required");
        BufferedImage image = ImageIO.read(new File(args[0]));
        if (image == null) throw new AssertionError("screenshot is not a PNG");
        if (image.getWidth() < 320 || image.getHeight() < 240)
            throw new AssertionError("screenshot is too small: " +
                image.getWidth() + "x" + image.getHeight());
        Set<Integer> colors = new HashSet<Integer>();
        int xStep = Math.max(1, image.getWidth() / 32);
        int yStep = Math.max(1, image.getHeight() / 32);
        for (int y = 0; y < image.getHeight(); y += yStep)
            for (int x = 0; x < image.getWidth(); x += xStep)
                colors.add(image.getRGB(x, y));
        if (colors.size() < 3)
            throw new AssertionError("screenshot appears blank; sampled colors=" +
                colors.size());
        System.out.println("launcher3Visual=ok size=" + image.getWidth() + "x" +
            image.getHeight() + " colors=" + colors.size());
    }
}
