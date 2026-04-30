/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  EventDispatcher: "resource://gre/modules/Messaging.sys.mjs",
});

// Keyed by browsingContextGroupId. Each entry: { sourceBcId, videoRef, resolve, reject }
const gPendingPiP = new Map();

// Keyed by browsingContextGroupId for sessions that are actively cloning.
const gActivePiP = new Map();

export const AndroidPictureInPicture = {
  request(sourceWGP, videoRef) {
    const bc = sourceWGP.browsingContext;
    const bcGroupId = bc.group.id;
    const sourceBcId = bc.id;

    const { promise, resolve, reject } = Promise.withResolvers();
    gPendingPiP.set(bcGroupId, { sourceBcId, videoRef, resolve, reject });

    lazy.EventDispatcher.instance.sendRequest(
      "GeckoView:LaunchPictureInPicture",
      {
        browsingContextId: sourceBcId,
        browsingContextGroupId: bcGroupId,
      }
    );

    return promise;
  },

  close(bcGroupId) {
    if (!gActivePiP.has(bcGroupId)) {
      return Promise.resolve();
    }
    gActivePiP.delete(bcGroupId);
    lazy.EventDispatcher.instance.sendRequest(
      "GeckoView:ClosePictureInPicture"
    );
    return Promise.resolve();
  },

  onActorReady(parentActor, bcId, bcGroupId) {
    const pending = gPendingPiP.get(bcGroupId);
    if (!pending || bcId === pending.sourceBcId) {
      return;
    }
    gPendingPiP.delete(bcGroupId);
    this._setupPlayer(parentActor, pending, bcGroupId);
  },

  async _setupPlayer(playerParent, { videoRef, resolve, reject }, bcGroupId) {
    try {
      await playerParent.setupPlayer(videoRef);
      gActivePiP.set(bcGroupId, true);
      resolve();
    } catch (e) {
      reject(e);
    }
  },
};
