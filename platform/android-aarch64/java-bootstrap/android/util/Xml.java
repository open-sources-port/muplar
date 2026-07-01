package android.util;

import org.xmlpull.v1.XmlPullParser;

public final class Xml {
    private Xml() {}
    public static AttributeSet asAttributeSet(XmlPullParser parser) {
        return parser instanceof AttributeSet ? (AttributeSet)parser : null;
    }
}
