/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.pictureinpicture

import android.app.PictureInPictureParams
import android.content.res.Configuration
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.util.Rational
import androidx.appcompat.app.AppCompatActivity
import org.mozilla.fenix.R
import java.lang.ref.WeakReference
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
        PipSessionHolder.activeActivity = WeakReference(this)
        session.loadUri("about:blank")
        enterPictureInPictureMode(buildPipParams())
    }

    override fun onUserLeaveHint() {
        super.onUserLeaveHint()
        Log.d(TAG, "onUserLeaveHint: re-entering PiP mode")
        enterPictureInPictureMode(buildPipParams())
    }

    override fun onPictureInPictureModeChanged(
        isInPictureInPictureMode: Boolean,
        newConfig: Configuration,
    ) {
        super.onPictureInPictureModeChanged(isInPictureInPictureMode, newConfig)
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
        PipSessionHolder.activeActivity = null
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
