/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.pictureinpicture

import android.util.Log
import org.mozilla.geckoview.GeckoSession

object PipSessionHolder {
    private const val TAG = "PictureInPicture"

    var session: GeckoSession? = null
        set(value) {
            Log.d(TAG, "session set: ${if (value != null) "non-null" else "null"}")
            field = value
        }

    var sourceBrowsingContextId: Long = -1

    fun take(): GeckoSession? {
        val s = session
        Log.d(TAG, "take: returning ${if (s != null) "non-null" else "null"} session")
        session = null
        return s
    }
}
