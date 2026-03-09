/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.pictureinpicture

import android.content.Context
import android.content.Intent
import org.mozilla.gecko.EventDispatcher
import org.mozilla.gecko.util.BundleEventListener
import org.mozilla.gecko.util.EventCallback
import org.mozilla.gecko.util.GeckoBundle
import org.mozilla.geckoview.GeckoRuntime
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.GeckoSessionSettings

internal class PictureInPictureEventListener(
    private val context: Context,
    private val runtime: GeckoRuntime,
) : BundleEventListener {

    override fun handleMessage(event: String, message: GeckoBundle?, callback: EventCallback?) {
        when (event) {
            LAUNCH_PIP -> {
                val bcId = message?.getLong("browsingContextId", -1L) ?: -1L
                val bcgId = message?.getLong("browsingContextGroupId", -1L) ?: -1L
                val pipSettings = GeckoSessionSettings.Builder()
                    .browsingContextGroupId(bcgId)
                    .build()
                val pipSession = GeckoSession(pipSettings)
                pipSession.open(runtime)
                PipSessionHolder.sourceBrowsingContextId = bcId
                PipSessionHolder.session = pipSession
                context.startActivity(
                    Intent(context, PictureInPictureActivity::class.java)
                        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
                )
            }
            CLOSE_PIP -> PipSessionHolder.activeActivity?.get()?.finish()
            FULLSCREEN_STARTED -> {
                PipSessionHolder.fullscreenBrowsingContextId =
                    message?.getLong("browsingContextId", -1L) ?: -1L
                PipSessionHolder.fullscreenBrowsingContextGroupId =
                    message?.getLong("browsingContextGroupId", -1L) ?: -1L
                PipBubble.show(context)
            }
            FULLSCREEN_ENDED -> {
                PipSessionHolder.fullscreenBrowsingContextId = -1L
                PipSessionHolder.fullscreenBrowsingContextGroupId = -1L
                PipBubble.hide(context)
            }
        }
    }

    fun register() {
        EventDispatcher.getInstance().registerUiThreadListener(
            this,
            LAUNCH_PIP,
            CLOSE_PIP,
            FULLSCREEN_STARTED,
            FULLSCREEN_ENDED,
        )
    }

    private companion object {
        const val LAUNCH_PIP = "GeckoView:LaunchPictureInPicture"
        const val CLOSE_PIP = "GeckoView:ClosePictureInPicture"
        const val FULLSCREEN_STARTED = "GeckoView:VideoFullscreenStarted"
        const val FULLSCREEN_ENDED = "GeckoView:VideoFullscreenEnded"
    }
}
