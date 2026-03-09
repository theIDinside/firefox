/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { GeckoViewUtils } from "resource://gre/modules/GeckoViewUtils.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  EventDispatcher: "resource://gre/modules/Messaging.sys.mjs",
});

const { debug } = GeckoViewUtils.initLogging("AndroidPictureInPicture[P]");

// Keyed by browsingContextGroupId. Each entry: { sourceBcId, videoRef, resolve, reject }
const gPendingPiP = new Map();

// Keyed by browsingContextGroupId for sessions that are actively cloning.
const gActivePiP = new Map();

// { browsingContextId, browsingContextGroupId, videoRef } while a video is fullscreened.
let gLastNotifiedFullscreenContext = null;

export const AndroidPictureInPicture = {
  onEvent(event, data, callback) {
    debug`AndroidPictureInPicture:onEvent ${event}`;
    if (event === "GeckoView:UserAgentPictureInPictureRequest") {
      if (gLastNotifiedFullscreenContext) {
        const { browsingContextId, browsingContextGroupId, videoRef } =
          gLastNotifiedFullscreenContext;
        // Android has already launched PictureInPictureActivity synchronously.
        // Just register the pending cloning — do not send GeckoView:LaunchPictureInPicture.
        this.preparePending(
          browsingContextId,
          browsingContextGroupId,
          videoRef
        );
        // Exit fullscreen immediately; the videoRef is already captured so it
        // remains valid regardless of fullscreen state.
        BrowsingContext.get(browsingContextId)
          ?.currentWindowContext.getActor("ContentDelegate")
          ?.sendAsyncMessage("GeckoView:ExitFullscreen");
      }
    }
  },

  preparePending(sourceBcId, bcGroupId, videoRef) {
    const { promise, resolve, reject } = Promise.withResolvers();
    gPendingPiP.set(bcGroupId, { sourceBcId, videoRef, resolve, reject });
    return promise;
  },

  doRequest(sourceBcId, bcGroupId, videoRef) {
    const promise = this.preparePending(sourceBcId, bcGroupId, videoRef);

    lazy.EventDispatcher.instance.sendRequest({
      type: "GeckoView:LaunchPictureInPicture",
      browsingContextId: sourceBcId,
      browsingContextGroupId: bcGroupId,
    });

    return promise;
  },

  request(sourceWGP, videoRef) {
    const bc = sourceWGP.browsingContext;
    const bcGroupId = bc.group.id;
    const sourceBcId = bc.id;

    return this.doRequest(sourceBcId, bcGroupId, videoRef);
  },

  close(bcGroupId) {
    if (!gActivePiP.has(bcGroupId)) {
      return Promise.resolve();
    }
    gActivePiP.delete(bcGroupId);
    lazy.EventDispatcher.instance.sendRequest({
      type: "GeckoView:ClosePictureInPicture",
    });
    return Promise.resolve();
  },

  setFullscreenContext({
    browsingContextId,
    browsingContextGroupId,
    videoRef,
  }) {
    debug`AndroidPictureInPicture:setFullscreenContext bc=${browsingContextId}, bcg=${browsingContextGroupId}, video=${videoRef}`;
    gLastNotifiedFullscreenContext = {
      browsingContextId,
      browsingContextGroupId,
      videoRef,
    };
    lazy.EventDispatcher.instance.sendRequest({
      type: "GeckoView:VideoFullscreenStarted",
      browsingContextId,
      browsingContextGroupId,
    });
  },

  clearFullscreenContext() {
    if (!gLastNotifiedFullscreenContext) {
      return;
    }
    gLastNotifiedFullscreenContext = null;
    lazy.EventDispatcher.instance.sendRequest({
      type: "GeckoView:VideoFullscreenEnded",
    });
  },

  onActorReady(parentActor, bcId, bcGroupId) {
    const pending = gPendingPiP.get(bcGroupId);
    if (!pending || bcId === pending.sourceBcId) {
      return;
    }
    gPendingPiP.delete(bcGroupId);
    let { videoRef, resolve, reject } = pending;
    try {
      parentActor.setupPlayer(videoRef).then(() => {
        gActivePiP.set(bcGroupId, true);
        resolve();
      });
    } catch (e) {
      reject(e);
    }
  },
};
