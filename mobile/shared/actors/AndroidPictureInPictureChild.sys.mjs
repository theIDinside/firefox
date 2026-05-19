/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { GeckoViewActorChild } from "resource://gre/modules/GeckoViewActorChild.sys.mjs";
import { GeckoViewUtils } from "resource://gre/modules/GeckoViewUtils.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  ContentDOMReference: "resource://gre/modules/ContentDOMReference.sys.mjs",
});

const { debug, warn } = GeckoViewUtils.initLogging(
  "AndroidPictureInPicture[C]"
);

export class AndroidPictureInPictureChild extends GeckoViewActorChild {
  handleEvent(event) {
    if (event.type === "DOMContentLoaded" && event.target === this.document) {
      this.sendAsyncMessage("AndroidPiP:Ready");
      return;
    }

    const video = event.target;
    if (!HTMLVideoElement.isInstance(video)) {
      return;
    }

    if (event.type === "playing") {
      if (video.videoWidth > 0 && video.videoHeight > 0) {
        this.sendAsyncMessage("AndroidPiP:VideoActive", {
          videoRef: lazy.ContentDOMReference.get(video),
        });
      }
    } else if (event.type === "pause" || event.type === "ended") {
      this.sendAsyncMessage("AndroidPiP:VideoInactive");
    }
  }

  async receiveMessage(msg) {
    debug`receiveMessage: ${msg.name}`;
    switch (msg.name) {
      case "AndroidPiP:SetupPlayer": {
        const { videoRef } = msg.data;
        const bc = this.manager.browsingContext;
        debug`SetupPlayer: my bcId=${bc.id} bcgId=${bc.group.id} videoRef.browsingContextId=${videoRef.browsingContextId}`;
        const sourceVideo = await lazy.ContentDOMReference.resolve(videoRef);
        if (!sourceVideo) {
          warn`SetupPlayer: could not resolve source video ${videoRef}`;
          throw new Error("Could not resolve source video");
        }

        const doc = this.document;
        const playerVideo = doc.createElement("video");
        playerVideo.style.width = "100vw";
        playerVideo.style.height = "100vh";
        playerVideo.style.backgroundColor = "#000";
        doc.body.style.margin = "0";
        doc.body.style.overflow = "hidden";
        doc.body.appendChild(playerVideo);

        this.contentWindow.addEventListener(
          "unload",
          () => {
            sourceVideo.stopCloningElementVisually();
          },
          { once: true }
        );

        await sourceVideo.cloneElementVisually(playerVideo);
        debug`SetupPlayer: cloning started`;
        return null;
      }
    }
    return null;
  }
}
