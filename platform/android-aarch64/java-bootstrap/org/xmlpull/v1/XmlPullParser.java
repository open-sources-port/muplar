package org.xmlpull.v1;

import java.io.IOException;

public interface XmlPullParser {
    int START_DOCUMENT = 0;
    int END_DOCUMENT = 1;
    int START_TAG = 2;
    int END_TAG = 3;
    int TEXT = 4;
    int getEventType() throws XmlPullParserException;
    int next() throws XmlPullParserException, IOException;
    int nextTag() throws XmlPullParserException, IOException;
    String getName();
    String getText();
    int getDepth();
    int getAttributeCount();
    String getAttributeName(int index);
    String getAttributeValue(int index);
    String getAttributeValue(String namespace, String name);
}
