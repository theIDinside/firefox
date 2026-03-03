/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.pictureinpicture

import android.app.PictureInPictureParams
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.util.Rational
import androidx.appcompat.app.AppCompatActivity
import org.mozilla.fenix.R
import org.mozilla.gecko.EventDispatcher
import org.mozilla.gecko.util.GeckoBundle
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.GeckoView

class PictureInPictureActivity : AppCompatActivity() {

    private lateinit var geckoView: GeckoView
    private var geckoSession: GeckoSession? = null

    companion object {
        private const val TAG = "PictureInPicture"
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Log.d(TAG, "onCreate: starting")

        setContentView(R.layout.activity_picture_in_picture)
        geckoView = findViewById(R.id.pip_gecko_view)

        val session = PipSessionHolder.take()
        if (session == null) {
            finish()
            return
        }

        geckoSession = session
        geckoView.setSession(session)
        // This is the only way I've found yet that'll work.
        // It loads a data doc and the setting of browsing context group
        // means it ends up in the process we want.
        session.loadUri("data:text/html,<html><body style='margin:0;background:magenta'><video id='playervideo' style='width:100vw;height:100vh'></video><script>dump('Test log to see we're in the right process')</script></body></html>")
        val entered = enterPictureInPictureMode(buildPipParams())
    }

    override fun onUserLeaveHint() {
        super.onUserLeaveHint()
        Log.d(TAG, "onUserLeaveHint: re-entering PiP mode")
        enterPictureInPictureMode(buildPipParams())
    }

    override fun onPictureInPictureModeChanged(isInPictureInPictureMode: Boolean) {
        super.onPictureInPictureModeChanged(isInPictureInPictureMode)
        Log.d(TAG, "onPictureInPictureModeChanged: $isInPictureInPictureMode")
        if (!isInPictureInPictureMode) {
            Log.d(TAG, "onPictureInPictureModeChanged: exited PiP, finishing activity")
            finish()
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        val sourceBcId = PipSessionHolder.sourceBrowsingContextId
        if (sourceBcId != -1L) {
            val bundle = GeckoBundle()
            bundle.putLong("browsingContextId", sourceBcId)
            EventDispatcher.getInstance().dispatch("GeckoView:PictureInPictureStopped", bundle)
        }
        geckoSession?.let { session ->
            geckoView.releaseSession()
            session.close()
        }
        geckoSession = null
        PipSessionHolder.session = null
        PipSessionHolder.sourceBrowsingContextId = -1
        Log.d(TAG, "onDestroy: done")
    }

    private fun buildPipParams(): PictureInPictureParams {
        val builder = PictureInPictureParams.Builder()
            .setAspectRatio(Rational(16, 9))
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            builder.setAutoEnterEnabled(true)
        }
        return builder.build()
    }
}
