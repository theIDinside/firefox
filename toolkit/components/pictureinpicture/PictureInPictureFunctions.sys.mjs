/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

class PictureInPictureFunctionsImpl {
  QueryInterface = ChromeUtils.generateQI(["nsIPictureInPictureFunctions"]);

  RequestPictureInPictureWindow(videoElement, pictureInPictureWindow) {
    if (!videoElement || !pictureInPictureWindow || !videoElement.ownerDocument) {
      throw Components.Exception(
        "Invalid video element",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    const docShell = videoElement.ownerGlobal.docShell;
    const windowGlobalChild = docShell.domWindow.windowGlobalChild;

    if (!windowGlobalChild) {
      throw Components.Exception(
        "No WindowGlobalChild available",
        Cr.NS_ERROR_FAILURE
      );
    }

    const actor = windowGlobalChild.getActor("PictureInPictureLauncher");
    if (!actor) {
      throw Components.Exception(
        "PictureInPictureLauncher actor not found",
        Cr.NS_ERROR_FAILURE
      );
    }

    return actor.togglePictureInPicture({
      video: videoElement,
      reason: "ApiRequest",
      pictureInPictureWindow,
      eventExtraKeys: {}
    });
  }
}

export function PictureInPictureFunctions() {
  return new PictureInPictureFunctionsImpl();
}
