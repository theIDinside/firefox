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

// Deferred docShell deactivation callbacks — set when setActive(false) arrives
// while PiP is active. Keyed by browsingContextGroupId.
const gDeactivateCallbacks = new Map();

// Keyed by browsingContextGroupId. Each entry: { wgp, videoRef }
const gActiveVideoRef = new Map();

export const AndroidPictureInPicture = {
  request(sourceWGP, videoRef) {
    const bc = sourceWGP.browsingContext;
    const bcGroupId = bc.group.id;
    const sourceBcId = bc.id;
    const remoteType = sourceWGP.domProcess.remoteType;

    const { promise, resolve, reject } = Promise.withResolvers();
    gPendingPiP.set(bcGroupId, { sourceBcId, videoRef, resolve, reject });

    lazy.EventDispatcher.instance.sendRequest({
      type: "GeckoView:LaunchPictureInPicture",
      browsingContextId: sourceBcId,
      browsingContextGroupId: bcGroupId,
      remoteType,
    });

    return promise;
  },

  close(bcGroupId) {
    if (!gActivePiP.has(bcGroupId)) {
      return Promise.resolve();
    }
    gActivePiP.delete(bcGroupId);
    const deactivate = gDeactivateCallbacks.get(bcGroupId);
    if (deactivate) {
      gDeactivateCallbacks.delete(bcGroupId);
      deactivate();
    }
    lazy.EventDispatcher.instance.sendRequest({
      type: "GeckoView:ClosePictureInPicture",
    });
    return Promise.resolve();
  },

  hasActivePiP(bcGroupId) {
    return gActivePiP.has(bcGroupId);
  },

  setDeactivateCallback(bcGroupId, cb) {
    gDeactivateCallbacks.set(bcGroupId, cb);
  },

  clearDeactivateCallback(bcGroupId) {
    gDeactivateCallbacks.delete(bcGroupId);
  },

  onActorReady(parentActor, bcId, bcGroupId) {
    const pending = gPendingPiP.get(bcGroupId);
    const isSource = !pending || bcId === pending.sourceBcId;
    const role = isSource ? "source" : "player";
    const sourcePid = pending
      ? BrowsingContext.get(pending.sourceBcId)?.currentWindowGlobal?.osPid
      : undefined;
    dump(
      `[AndroidPiP] onActorReady role=${role} bcId=${bcId} bcgId=${bcGroupId} actorOsPid=${parentActor.manager.osPid} sourceBcId=${pending?.sourceBcId} sourceOsPid=${sourcePid}\n`
    );
    if (isSource) {
      return;
    }
    gPendingPiP.delete(bcGroupId);
    this._setupPlayer(parentActor, pending, bcGroupId);
  },

  setActiveVideoRef(bcGroupId, wgp, videoRef) {
    gActiveVideoRef.set(bcGroupId, { wgp, videoRef });
  },

  clearActiveVideoRef(bcGroupId) {
    gActiveVideoRef.delete(bcGroupId);
  },

  getActiveVideoRef(bcGroupId) {
    return gActiveVideoRef.get(bcGroupId) ?? null;
  },

  async _setupPlayer(playerParent, { videoRef, resolve, reject }, bcGroupId) {
    try {
      await playerParent.setupPlayer(videoRef);
      gActivePiP.set(bcGroupId, true);
      resolve();
    } catch (e) {
      lazy.EventDispatcher.instance.sendRequest({
        type: "GeckoView:ClosePictureInPicture",
      });
      reject(e);
    }
  },
};
