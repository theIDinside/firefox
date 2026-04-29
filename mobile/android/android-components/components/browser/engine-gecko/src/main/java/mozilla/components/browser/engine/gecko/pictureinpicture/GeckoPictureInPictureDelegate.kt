/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko.pictureinpicture

import org.mozilla.geckoview.PictureInPictureController

/**
 * [PictureInPictureController.Delegate] implementation that dispatches picture-in-picture
 * lifecycle events to the provided callbacks.
 *
 * @param onEnterPictureInPicture Called when a page requests picture-in-picture, with the
 *   browsing context ID and browsing context group ID of the requesting page.
 * @param onExitPictureInPicture Called when the picture-in-picture window should be closed.
 */
class GeckoPictureInPictureDelegate(
    private val onEnterPictureInPicture: (browsingContextId: Long, browsingContextGroupId: Long, remoteType: String?) -> Unit,
    private val onExitPictureInPicture: () -> Unit,
) : PictureInPictureController.Delegate {
    override fun onEnterPictureInPicture(browsingContextId: Long, browsingContextGroupId: Long, remoteType: String?) {
        onEnterPictureInPicture.invoke(browsingContextId, browsingContextGroupId, remoteType)
    }

    override fun onExitPictureInPicture() {
        onExitPictureInPicture.invoke()
    }
}
