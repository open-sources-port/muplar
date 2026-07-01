package org.xmlpull.v1;

public class XmlPullParserException extends Exception {
    public XmlPullParserException(String message) { super(message); }
    public XmlPullParserException(String message, Object parser, Throwable cause) {
        super(message, cause);
    }
}
