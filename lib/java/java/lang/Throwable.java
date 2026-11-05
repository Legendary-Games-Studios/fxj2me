package java.lang;

public class Throwable {
    private String message;
    private Throwable cause;

    public Throwable() { }
    public Throwable(String message) { this.message = message; }
    public Throwable(String message, Throwable cause) {
        this.message = message;
        this.cause = cause;
    }

    public String getMessage() { return message; }
    public Throwable getCause() { return cause; }
    public Throwable initCause(Throwable cause) {
        this.cause = cause;
        return this;
    }

    public String toString() {
        String name = getClass().getName();
        return message == null ? name : name + ": " + message;
    }
}
