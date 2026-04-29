/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.media.ext

import android.support.v4.media.session.PlaybackStateCompat
import mozilla.components.browser.state.state.MediaSessionState
import mozilla.components.concept.engine.mediasession.MediaSession
import mozilla.components.feature.media.R

internal const val CUSTOM_ACTION_PIP = "mozac.feature.media.action.PIP"

/**
 * Turns the [MediaSessionState] into a [PlaybackStateCompat] to be used with a `MediaSession`.
 */
internal fun MediaSessionState.toPlaybackState(): PlaybackStateCompat {
    val builder = PlaybackStateCompat.Builder()
        .setActions(
            PlaybackStateCompat.ACTION_PLAY_PAUSE or
                PlaybackStateCompat.ACTION_PLAY or
                PlaybackStateCompat.ACTION_PAUSE,
        )
        .setState(
            when (playbackState) {
                MediaSession.PlaybackState.PLAYING -> PlaybackStateCompat.STATE_PLAYING
                MediaSession.PlaybackState.PAUSED -> PlaybackStateCompat.STATE_PAUSED
                else -> PlaybackStateCompat.STATE_NONE
            },
            // Time state not exposed yet:
            // https://github.com/mozilla-mobile/android-components/issues/2458
            PlaybackStateCompat.PLAYBACK_POSITION_UNKNOWN,
            when (playbackState) {
                // The actual playback speed is not exposed yet:
                // https://github.com/mozilla-mobile/android-components/issues/2459
                MediaSession.PlaybackState.PLAYING -> 1.0f
                else -> 0.0f
            },
        )

    if (playbackState == MediaSession.PlaybackState.PLAYING) {
        builder.addCustomAction(
            PlaybackStateCompat.CustomAction.Builder(
                CUSTOM_ACTION_PIP,
                "Picture-in-Picture",
                R.drawable.mozac_feature_media_action_pip,
            ).build(),
        )
    }

    return builder.build()
}

/**
 * If this state is [MediaSession.PlaybackState.PLAYING] then return true, else return false.
 */
fun MediaSessionState.playing(): Boolean {
    return when (playbackState) {
        MediaSession.PlaybackState.PLAYING -> true
        else -> false
    }
}
