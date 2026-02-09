/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { PictureInPictureChild } from "resource://gre/actors/PictureInPictureChild.sys.mjs";

/**
 * Desktops implementation of the PIP Chrome JS interface.
 */
class PictureInPictureFunctionsImpl {
  QueryInterface = ChromeUtils.generateQI(["nsIPictureInPictureFunctions"]);

  #getActor(videoElement, actorType) {
    if (!videoElement || !videoElement.ownerDocument) {
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

    const actor = windowGlobalChild.getActor(actorType);
    if (!actor) {
      throw Components.Exception(
        `${actorType} actor not found`,
        Cr.NS_ERROR_FAILURE
      );
    }
    return actor;
  }

  OpenPictureInPictureWindow(videoElement, pictureInPictureWindow) {
    if (!pictureInPictureWindow) {
      throw Components.Exception(
        "Invalid PictureInPictureWindow argument",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    return this.#getActor(
      videoElement,
      "PictureInPictureLauncher"
    ).togglePictureInPicture({
      video: videoElement,
      reason: "Request",
      pictureInPictureWindow,
      eventExtraKeys: {},
    });
  }

  ClosePictureInPictureWindow(videoElement) {
    if (!videoElement) {
      throw Components.Exception(
        "Invalid PictureInPictureWindow argument",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    const actor =
      PictureInPictureChild?.webContentElementToPiPContextWeakMap.get(
        videoElement
      );
    if (actor) {
      return actor.closePictureInPicture({ reason: "Programmatic" });
    }
    throw Components.Exception("No actor found available", Cr.NS_ERROR_FAILURE);
  }
}

export function PictureInPictureFunctions() {
  return new PictureInPictureFunctionsImpl();
}
