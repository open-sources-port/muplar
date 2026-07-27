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
        int w = image.getWidth();
        int h = image.getHeight();
        if (w < 320 || h < 240)
            throw new AssertionError("screenshot is too small: " + w + "x" + h);

        Set<Integer> colors = new HashSet<Integer>();
        Set<Integer> topColors = new HashSet<Integer>();
        Set<Integer> midColors = new HashSet<Integer>();
        Set<Integer> botColors = new HashSet<Integer>();

        int xStep = Math.max(1, w / 32);
        int yStep = Math.max(1, h / 32);
        for (int y = 0; y < h; y += yStep) {
            for (int x = 0; x < w; x += xStep) {
                int rgb = image.getRGB(x, y);
                colors.add(rgb);
                if (y < h * 0.10) {
                    topColors.add(rgb);
                } else if (y > h * 0.90) {
                    botColors.add(rgb);
                } else {
                    midColors.add(rgb);
                }
            }
        }

        if (colors.size() < 3)
            throw new AssertionError("screenshot appears blank; sampled colors=" +
                colors.size());

        if (topColors.isEmpty() || midColors.isEmpty() || botColors.isEmpty())
            throw new AssertionError("missing visual region sampling: top=" +
                topColors.size() + " mid=" + midColors.size() + " bot=" + botColors.size());

        System.out.println("launcher3Visual=ok size=" + w + "x" + h +
            " colors=" + colors.size() + " (top=" + topColors.size() +
            " mid=" + midColors.size() + " bot=" + botColors.size() + ")");
    }
}
