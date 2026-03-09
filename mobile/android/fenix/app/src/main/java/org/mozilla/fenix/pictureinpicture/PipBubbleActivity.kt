/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.pictureinpicture

import android.content.Intent
import android.os.Bundle
import android.util.Log
import androidx.appcompat.app.AppCompatActivity
import org.mozilla.fenix.ext.components
import org.mozilla.gecko.EventDispatcher
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.GeckoSessionSettings

class PipBubbleActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val bcGroupId = PipSessionHolder.fullscreenBrowsingContextGroupId
        val bcId = PipSessionHolder.fullscreenBrowsingContextId
        Log.d("PipBubble", "PipBubbleActivity.onCreate bcId=$bcId bcGroupId=$bcGroupId")
        if (bcGroupId == -1L || bcId == -1L) {
            Log.w("PipBubble", "PipBubbleActivity: invalid BC IDs, finishing")
            finish()
            return
        }
        EventDispatcher.getInstance().dispatch("GeckoView:UserAgentPictureInPictureRequest", null)
        val runtime = components.core.geckoRuntime
        val pipSession = GeckoSession(
            GeckoSessionSettings.Builder().browsingContextGroupId(bcGroupId).build(),
        )
        pipSession.open(runtime)
        PipSessionHolder.sourceBrowsingContextId = bcId
        PipSessionHolder.session = pipSession
        Log.d("PipBubble", "PipBubbleActivity: launching PictureInPictureActivity")
        startActivity(
            Intent(this, PictureInPictureActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
        )
        finish()
    }
}
