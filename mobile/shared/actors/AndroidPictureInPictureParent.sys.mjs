/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { GeckoViewActorParent } from "resource://gre/modules/GeckoViewActorParent.sys.mjs";
import { GeckoViewUtils } from "resource://gre/modules/GeckoViewUtils.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AndroidPictureInPicture:
    "resource://gre/modules/AndroidPictureInPicture.sys.mjs",
});

const { debug, warn } = GeckoViewUtils.initLogging(
  "AndroidPictureInPicture[P]"
);

export class AndroidPictureInPictureParent extends GeckoViewActorParent {
  receiveMessage(msg) {
    debug`receiveMessage: ${msg.name}`;
    const bc = this.manager.browsingContext;
    switch (msg.name) {
      case "AndroidPiP:Request": {
        return lazy.AndroidPictureInPicture.request(
          this.manager,
          msg.data.videoRef
        );
      }
      case "AndroidPiP:Ready": {
        lazy.AndroidPictureInPicture.onActorReady(this, bc.id, bc.group.id);
        return null;
      }
      case "AndroidPiP:Close": {
        return lazy.AndroidPictureInPicture.close(bc.group.id);
      }
    }
    return null;
  }

  setupPlayer(videoRef) {
    return this.sendQuery("AndroidPiP:SetupPlayer", { videoRef });
  }
}
