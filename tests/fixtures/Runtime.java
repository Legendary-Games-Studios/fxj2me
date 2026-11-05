/* fxj2me test fixture: string + exception semantics, exercised by
 * tests/test_runtime.c against the boot class library. */
public class Runtime {
    /*--- strings ----------------------------------------------------*/

    public static String lit() { return "fxj2me"; }

    /* Literal identity: javac's constant pool must intern equal to
     * vm_make_string's intern table, so == holds for equal literals. */
    public static boolean interned() {
        String a = "fxj2me";
        String b = "fxj2me";
        return a == b;
    }

    public static int strLen() { return "fxj2me".length(); }
    public static char strAt() { return "fxj2me".charAt(2); }
    public static boolean strEq() {
        return "abc".equals("abc") && !"abc".equals("abd");
    }
    public static String strCat() {
        int n = 7;
        return "n=" + n + "!";
    }
    public static String strSub() { return "abcdef".substring(2, 5); }
    public static int parse() { return Integer.parseInt("42"); }
    public static String unparse() { return Integer.toString(-17); }

    /*--- exceptions -------------------------------------------------*/

    public static int catchAIOOBE() {
        try {
            int[] a = new int[2];
            return a[5];
        } catch (ArrayIndexOutOfBoundsException e) {
            return 1;
        }
    }

    public static int catchDiv0() {
        try {
            int x = 1 / 0;
            return x;
        } catch (ArithmeticException e) {
            return 2;
        }
    }

    public static int catchThrown() {
        try {
            throw new IllegalStateException("boom");
        } catch (IllegalStateException e) {
            return 3;
        }
    }

    /* caught as a superclass */
    public static int catchSuper() {
        try {
            throw new ClassCastException("sub");
        } catch (RuntimeException e) {
            return 4;
        }
    }

    /* thrown deep, caught frames above */
    public static int nestedUnwind() {
        try {
            deep(3);
            return -1;
        } catch (NullPointerException e) {
            return 5;
        }
    }

    private static int deep(int n) {
        if (n == 0)
            throw new NullPointerException("deep");
        return deep(n - 1);
    }

    /* rethrow replaces the pending exception */
    public static int rethrow() {
        try {
            try {
                throw new IllegalArgumentException("inner");
            } catch (IllegalArgumentException e) {
                throw new IllegalStateException("outer");
            }
        } catch (IllegalStateException e) {
            return 6;
        }
    }

    public static int catchNegArray() {
        try {
            int[] a = new int[-1];
            return a.length;
        } catch (NegativeArraySizeException e) {
            return 7;
        }
    }

    /* implicit NPE, caught */
    public static int catchNPE() {
        try {
            String s = null;
            return s.length();
        } catch (NullPointerException e) {
            return 8;
        }
    }

    /* finally runs after catch */
    public static int finallyOrder() {
        int order = 0;
        try {
            order += 10;
            throw new Exception("x");
        } catch (Exception e) {
            order += 100;
        } finally {
            order += 1000;
        }
        return order;
    }

    public static String caughtMsg() {
        try {
            throw new IllegalStateException("boom");
        } catch (IllegalStateException e) {
            return e.getMessage();
        }
    }

    /* uncaught: the VM must report FX_ERR_JAVA with the formatted text */
    public static String uncaught() {
        throw new IllegalArgumentException("propagate");
    }

    /* uncaught implicit NPE */
    public static int nullDeref() {
        String s = null;
        return s.length();
    }
}
