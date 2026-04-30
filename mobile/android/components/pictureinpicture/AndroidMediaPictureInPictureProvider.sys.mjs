/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  ContentDOMReference: "resource://gre/modules/ContentDOMReference.sys.mjs",
});

class PictureInPictureProviderImplementation {
  QueryInterface = ChromeUtils.generateQI(["nsIMediaPictureInPictureProvider"]);

  openMediaPictureInPictureWindow(videoElement, pictureInPictureWindow) {
    if (!pictureInPictureWindow) {
      throw Components.Exception(
        "Invalid PictureInPictureWindow argument",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    if (!videoElement) {
      throw Components.Exception(
        "Invalid video element",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    const windowGlobalChild = videoElement.ownerGlobal.windowGlobalChild;
    if (!windowGlobalChild) {
      throw Components.Exception(
        "No WindowGlobalChild available",
        Cr.NS_ERROR_FAILURE
      );
    }

    const actor = windowGlobalChild.getActor("AndroidPictureInPicture");
    return actor.sendQuery("AndroidPiP:Request", {
      videoRef: lazy.ContentDOMReference.get(videoElement),
    });
  }

  closeMediaPictureInPictureWindow(videoElement) {
    if (!videoElement) {
      throw Components.Exception(
        "Invalid video element",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    const windowGlobalChild = videoElement.ownerGlobal.windowGlobalChild;
    if (!windowGlobalChild) {
      throw Components.Exception(
        "No WindowGlobalChild available",
        Cr.NS_ERROR_FAILURE
      );
    }

    const actor = windowGlobalChild.getActor("AndroidPictureInPicture");
    return actor.sendQuery("AndroidPiP:Close");
  }
}

export function PictureInPictureProvider() {
  return new PictureInPictureProviderImplementation();
}
