/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview;

import android.util.Log;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.UiThread;
import org.mozilla.gecko.EventDispatcher;
import org.mozilla.gecko.util.BundleEventListener;
import org.mozilla.gecko.util.EventCallback;
import org.mozilla.gecko.util.GeckoBundle;

public class PictureInPictureController {

  public interface Delegate {
    @UiThread
    default void onEnterPictureInPicture(
        final long browsingContextId,
        final long browsingContextGroupId,
        final @Nullable String remoteType) {}

    @UiThread
    default void onExitPictureInPicture() {}
  }

  /* package */ static final class PictureInPictureProxy implements BundleEventListener {
    private static final String LAUNCH_EVENT = "GeckoView:LaunchPictureInPicture";
    private static final String CLOSE_EVENT = "GeckoView:ClosePictureInPicture";
    private static final String LOG_TAG = "PictureInPicture";

    private @Nullable Delegate mDelegate;

    public synchronized void setDelegate(final @Nullable Delegate delegate) {
      if (mDelegate == delegate) {
        return;
      }
      if (mDelegate != null) {
        unregisterListener();
      }
      mDelegate = delegate;
      if (mDelegate != null) {
        registerListener();
      }
    }

    public synchronized @Nullable Delegate getDelegate() {
      return mDelegate;
    }

    private void registerListener() {
      EventDispatcher.getInstance().registerUiThreadListener(this, LAUNCH_EVENT, CLOSE_EVENT);
    }

    private void unregisterListener() {
      EventDispatcher.getInstance().unregisterUiThreadListener(this, LAUNCH_EVENT, CLOSE_EVENT);
    }

    @Override
    public synchronized void handleMessage(
        @NonNull final String event,
        final GeckoBundle message,
        final EventCallback callback) {
      if (mDelegate == null) {
        if (callback != null) {
          callback.sendError("No delegate attached");
        }
        return;
      }

      if (LAUNCH_EVENT.equals(event)) {
        final long bcId = message.getLong("browsingContextId", -1L);
        final long bcgId = message.getLong("browsingContextGroupId", -1L);
        final String remoteType = message.getString("remoteType");
        Log.d(LOG_TAG, "handleMessage: " + event + " bcId=" + bcId + " bcgId=" + bcgId + " remoteType=" + remoteType);
        mDelegate.onEnterPictureInPicture(bcId, bcgId, remoteType);
      } else if (CLOSE_EVENT.equals(event)) {
        mDelegate.onExitPictureInPicture();
      }
    }
  }
}
