/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.media.session

import android.os.Bundle
import android.support.v4.media.session.MediaSessionCompat
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.media.ext.findActiveMediaTab
import mozilla.components.feature.media.ext.CUSTOM_ACTION_PIP
import mozilla.components.support.base.log.logger.Logger

internal class MediaSessionCallback(
    private val store: BrowserStore,
) : MediaSessionCompat.Callback() {
    private val logger = Logger("MediaSessionCallback")

    override fun onPlay() {
        logger.debug("play()")

        store.state.findActiveMediaTab()?.mediaSessionState?.controller?.play()
    }

    override fun onPause() {
        logger.debug("pause()")

        store.state.findActiveMediaTab()?.mediaSessionState?.controller?.pause()
    }

    override fun onCustomAction(action: String, extras: Bundle?) {
        logger.debug("customAction: $action")

        if (action == CUSTOM_ACTION_PIP) {
            store.state.findActiveMediaTab()?.engineState?.engineSession?.requestPictureInPicture()
        }
    }
}
