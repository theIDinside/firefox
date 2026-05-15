/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

export class DOMFullscreenChild extends JSWindowActorChild {
  receiveMessage(aMessage) {
    let window = this.contentWindow;
    let windowUtils = window?.windowUtils;

    switch (aMessage.name) {
      case "DOMFullscreen:Entered": {
        if (!windowUtils) {
          // If we are not able to enter fullscreen, tell the parent to just
          // exit.
          this.sendAsyncMessage("DOMFullscreen:Exit", {});
          break;
        }

        let remoteFrameBC = aMessage.data.remoteFrameBC;
        if (remoteFrameBC) {
          let remoteFrame = remoteFrameBC.embedderElement;
          if (!remoteFrame) {
            // This could happen when the page navigate away and trigger a
            // process switching during fullscreen transition, tell the parent
            // to just exit.
            this.sendAsyncMessage("DOMFullscreen:Exit", {});
            break;
          }
          this._isNotTheRequestSource = true;
          windowUtils.remoteFrameFullscreenChanged(remoteFrame);
        } else if (
          !windowUtils.handleFullscreenRequests() &&
          !this.document.fullscreenElement
        ) {
          // If we don't actually have any pending fullscreen request
          // to handle, neither we have been in fullscreen, tell the
          // parent to just exit.
          this.sendAsyncMessage("DOMFullscreen:Exit", {});
        }
        break;
      }
      case "DOMFullscreen:CleanUp": {
        let isNotTheRequestSource = !!aMessage.data.remoteFrameBC;
        if (this.document.fullscreenElement) {
          this._isNotTheRequestSource = isNotTheRequestSource;
          // windowUtils could be null if the associated window is not current
          // active window. In this case, document must be in the process of
          // exiting fullscreen, it is okay to not ask it to exit fullscreen.
          if (windowUtils) {
            windowUtils.exitFullscreen();
          }
        } else if (isNotTheRequestSource) {
          // If we are not the request source and have exited fullscreen, reply
          // Exited to parent as parent is waiting for our reply.
          this.sendAsyncMessage("DOMFullscreen:Exited", {});
        } else {
          // Already exited fullscreen — ack the cleanup choreography so the
          // parent clears its waiting flag. The fullscreen-painted observer
          // notification is fired by FullscreenPaintBarrier in C++, not here.
          this.sendAsyncMessage("DOMFullscreen:Painted", {});
        }
        break;
      }
    }
  }

  handleEvent(aEvent) {
    if (this.hasBeenDestroyed()) {
      // Make sure that this actor is alive before going further because
      // if it's not the case, any attempt to send a message or access
      // objects such as 'contentWindow' will fail. (See bug 1590138)
      return;
    }

    switch (aEvent.type) {
      case "MozDOMFullscreen:Request": {
        this.sendAsyncMessage("DOMFullscreen:Request", {
          fullscreenKeyboardLock: aEvent.detail,
        });
        break;
      }
      case "MozDOMFullscreen:NewOrigin": {
        this.sendAsyncMessage("DOMFullscreen:NewOrigin", {
          originNoSuffix: aEvent.target.nodePrincipal.originNoSuffix,
        });
        break;
      }
      case "MozDOMFullscreen:Exit": {
        this.sendAsyncMessage("DOMFullscreen:Exit", {});
        break;
      }
      case "MozDOMFullscreen:Entered":
      case "MozDOMFullscreen:Exited": {
        if (this._isNotTheRequestSource) {
          // Fullscreen change event for a frame in the
          // middle (content frame embedding the oop frame where the
          // request comes from)

          delete this._isNotTheRequestSource;
          this.sendAsyncMessage(aEvent.type.replace("Moz", ""), {});
          break;
        }

        // We are the request source. On exit, ack the cleanup choreography
        // so the parent clears its waitingForChildExitFullscreen flag. The
        // fullscreen-painted observer notification is fired independently by
        // FullscreenPaintBarrier in C++.
        if (aEvent.type === "MozDOMFullscreen:Exited") {
          this.sendAsyncMessage("DOMFullscreen:Painted", {});
        }

        if (!this.document || !this.document.fullscreenElement) {
          // If we receive any fullscreen change event, and find we are
          // actually not in fullscreen, also ask the parent to exit to
          // ensure that the parent always exits fullscreen when we do.
          this.sendAsyncMessage("DOMFullscreen:Exit", {});
        }
        break;
      }
      case "MozDOMFullscreen:UpdateKeyboardLock": {
        this.sendAsyncMessage("DOMFullscreen:UpdateKeyboardLock", {
          fullscreenKeyboardLock: aEvent.detail,
        });
        break;
      }
    }
  }

  hasBeenDestroyed() {
    // The 'didDestroy' callback is not always getting called.
    // So we can't rely on it here. Instead, we will try to access
    // the browsing context to judge wether the actor has
    // been destroyed or not.
    try {
      return !this.browsingContext;
    } catch {
      return true;
    }
  }
}
