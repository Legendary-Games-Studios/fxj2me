/* fxj2me test fixture: a complete MIDlet with deterministic rendering,
 * driven by tests/test_midp.c. Every primitive paints into its own zone so
 * the test can assert individual frame pixels:
 *
 *   (0,0)      black   drawRect border
 *   text zone  black   drawString "Hello" at (64,10) HCENTER|TOP
 *   (32,42)    black iff count >= 1   fillRect(20,40,10+count*8,5)
 *   (103,50)   black   drawImage top row, (103,52) white
 *   (22,57)    black   drawRegion ROT180 (src row 0 -> bottom), (22,52) white
 *   (64,60)    black   drawLine
 *   (66,33)    black   fillTriangle
 *   (95,35)    black   fillArc (full ellipse)
 *   (10,2)     black   drawRoundRect top edge
 *   (31,31)    black   fillRect after translate(30,0)
 *   (0,50) black (1,50) white (0,51) white (1,51) black   drawRGB 2x2
 *   (66,57)    black   clipped fillRect, (61,57) white (outside clip)
 */
import javax.microedition.midlet.MIDlet;
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Command;
import javax.microedition.lcdui.CommandListener;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Displayable;
import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;

public class HelloMidlet extends MIDlet {
    static HelloMidlet owner;
    static boolean runnableRan;
    static boolean destroyed;
    static int count;
    static int paintCount;
    static String vendor;
    static String midlet1;
    static int rgbFirst;
    static int rgbSecond;
    static int commandFired;
    static String commandLabel;

    HelloCanvas canvas;

    protected void startApp() {
        owner = this;
        vendor = getAppProperty("MIDlet-Vendor");
        midlet1 = getAppProperty("MIDlet-1");
        canvas = new HelloCanvas();
        Display.getDisplay(this).setCurrent(canvas);
        Display.getDisplay(this).callSerially(new Runnable() {
            public void run() { runnableRan = true; }
        });
    }

    protected void pauseApp() { }

    protected void destroyApp(boolean unconditional) {
        destroyed = true;
    }

    static class HelloCanvas extends Canvas implements CommandListener {
        HelloCanvas() {
            addCommand(new Command("Go", Command.OK, 1));
            setCommandListener(this);
        }

        public void commandAction(Command c, Displayable d) {
            commandFired++;
            commandLabel = c.getLabel();
        }

        protected void paint(Graphics g) {
            Image img = Image.createImage(8, 8);
            Graphics ig = img.getGraphics();

            paintCount++;
            g.setColor(0xFFFFFF);
            g.fillRect(0, 0, 128, 64);
            g.setColor(0x000000);

            g.drawRect(0, 0, 128, 64);
            g.drawString("Hello", 64, 10, Graphics.HCENTER | Graphics.TOP);
            g.fillRect(20, 40, 10 + count * 8, 5);

            /* offscreen image: black top row, white below */
            ig.setColor(0x000000);
            ig.fillRect(0, 0, 8, 1);
            g.drawImage(img, 100, 50, Graphics.TOP | Graphics.LEFT);
            g.drawRegion(img, 0, 0, 8, 8, 3 /* ROT180 */, 20, 50,
                         Graphics.TOP | Graphics.LEFT);

            /* getRGB round-trip on the same image */
            int[] px4 = new int[64];
            img.getRGB(px4, 0, 8, 0, 0, 8, 8);
            rgbFirst = px4[0];     /* row 0: black  */
            rgbSecond = px4[16];   /* row 2: white  */

            g.drawLine(0, 60, 127, 60);
            g.fillTriangle(60, 30, 72, 30, 66, 38);
            g.fillArc(80, 20, 30, 30, 0, 360);
            g.drawRoundRect(2, 2, 20, 12, 6, 6);

            g.translate(30, 0);
            g.fillRect(0, 30, 4, 4);
            g.translate(-30, 0);

            g.drawRGB(new int[] { 0x000000, 0xFFFFFF,
                                  0xFFFFFF, 0x000000 },
                      0, 2, 0, 50, 2, 2, false);

            /* clip: only x >= 64 of the rect is drawn */
            g.setClip(64, 0, 64, 64);
            g.fillRect(60, 55, 20, 6);
        }

        protected void keyPressed(int keyCode) {
            count++;
            repaint();
            if (count >= 3 && owner != null)
                owner.notifyDestroyed();
        }
    }
}
