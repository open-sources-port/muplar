package android.content;

import android.os.Bundle;
import android.os.ICancellationSignal;
import android.os.IInterface;
import android.database.Cursor;
import android.net.Uri;

public interface IContentProvider extends IInterface {
    default Bundle call(AttributionSource attributionSource,
                        String authority,
                        String method,
                        String arg,
                        Bundle extras) {
        return Bundle.EMPTY;
    }

    default Cursor query(AttributionSource attributionSource,
                         Uri uri,
                         String[] projection,
                         Bundle queryArgs,
                         ICancellationSignal cancellationSignal) {
        return null;
    }
}
