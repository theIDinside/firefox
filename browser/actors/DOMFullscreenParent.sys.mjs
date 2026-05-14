/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

export class DOMFullscreenParent extends JSWindowActorParent {
  // These properties get set by browser-fullScreenAndPointerLock.js.
  // TODO: Bug 1743703 - Consider moving the messaging component of
  //       browser-fullScreenAndPointerLock.js into the actor
  waitingForChildEnterFullscreen = false;
  waitingForChildExitFullscreen = false;
  // Cache the next message recipient actor and in-process browsing context that
  // is computed by _getNextMsgRecipientActor() of
  // browser-fullScreenAndPointerLock.js, this is used to ensure the fullscreen
  // cleanup messages goes the same route as fullscreen request, especially for
  // the cleanup that happens after actor is destroyed.
  // TODO: Bug 1743703 - Consider moving the messaging component of
  //       browser-fullScreenAndPointerLock.js into the actor
  nextMsgRecipient = null;

  updateFullscreenWindowReference(aWindow) {
    if (aWindow.document.documentElement.hasAttribute("inDOMFullscreen")) {
      this._fullscreenWindow = aWindow;
    } else {
      delete this._fullscreenWindow;
    }
  }

  cleanupDomFullscreen(aWindow) {
    if (!aWindow.FullScreen) {
      return;
    }

    // If we don't need to wait for child reply, i.e. cleanupDomFullscreen
    // doesn't message to child, and we've exit the fullscreen, there won't be
    // DOMFullscreen:Painted message from child and it is possible that no more
    // paint would be triggered, so just notify fullscreen-painted observer.
    if (!this._cleanupDomFullscreen(aWindow) && !aWindow.document.fullscreen) {
      Services.obs.notifyObservers(aWindow, "fullscreen-painted");
    }
  }

  /**
   * Clean up fullscreen state and resume chrome UI if window is in fullscreen
   * and this actor is the one where the original fullscreen enter or
   * exit request comes.
   */
  _cleanupFullscreenStateAndResumeChromeUI(aWindow) {
    this.cleanupDomFullscreen(aWindow);
    if (this.requestOrigin == this && aWindow.document.fullscreen) {
      aWindow.windowUtils.remoteFrameFullscreenReverted();
    }
  }

  /**
   * Clean up full screen, starting from the request origin's first ancestor
   * frame that is OOP.
   *
   * If there are OOP ancestor frames, we notify the first of those and then bail to
   * be called again in that process when it has dealt with the change. This is
   * repeated until all ancestor processes have been updated. Once that has happened
   * we remove our handlers and attributes and notify the request origin to complete
   * the cleanup.
   */
  _cleanupDomFullscreen(aWindow) {
    let needToWaitForChildExit = false;
    // Use the message recipient cached in the actor if possible, especially for
    // the case that actor is destroyed, which we are unable to find it by
    // walking up the browsing context tree.
    let [target, inProcessBC] = this._getNextMsgRecipientActor(
      true /* aUseCache */
    );
    if (target) {
      needToWaitForChildExit = true;
      // Record that the actor is waiting for its child to exit fullscreen so
      // that if it dies we can continue cleanup.
      target.waitingForChildExitFullscreen = true;
      target.sendAsyncMessage("DOMFullscreen:CleanUp", {
        remoteFrameBC: inProcessBC,
      });
      if (inProcessBC) {
        return needToWaitForChildExit;
      }
    }

    aWindow.FullScreen.cleanupDomFullscreen();

    return needToWaitForChildExit;
  }

  /**
   * Search for the first ancestor of aActor that lives in a different process.
   * If found, that ancestor actor and the browsing context for its child which
   * was in process are returned. Otherwise [request origin, null].
   *
   * @param {bool} aUseCache
   *        Use the recipient cached in the aActor if available.
   *
   * @return {[JSWindowActorParent, BrowsingContext]}
   *         The parent actor which should be sent the next msg and the
   *         in process browsing context which is its child. Will be
   *         [null, null] if there is no OOP parent actor and request origin
   *         is unset. [null, null] is also returned if the intended actor or
   *         the calling actor has been destroyed or its associated
   *         WindowContext is in BFCache.
   */
  _getNextMsgRecipientActor(aUseCache) {
    // Walk up the cached nextMsgRecipient to find the next available actor if
    // any.
    if (aUseCache && this.nextMsgRecipient) {
      let nextMsgRecipient = this.nextMsgRecipient;
      while (nextMsgRecipient) {
        let [actor] = nextMsgRecipient;
        if (
          !actor.hasBeenDestroyed() &&
          actor.windowContext &&
          !actor.windowContext.isInBFCache
        ) {
          return nextMsgRecipient;
        }
        nextMsgRecipient = actor.nextMsgRecipient;
      }
    }

    if (this.hasBeenDestroyed()) {
      return [null, null];
    }

    let childBC = this.browsingContext;
    let parentBC = childBC.parent;

    // Walk up the browsing context tree from aActor's browsing context
    // to find the first ancestor browsing context that's in a different process.
    while (parentBC) {
      if (!childBC.currentWindowGlobal || !parentBC.currentWindowGlobal) {
        break;
      }
      let childPid = childBC.currentWindowGlobal.osPid;
      let parentPid = parentBC.currentWindowGlobal.osPid;

      if (childPid == parentPid) {
        childBC = parentBC;
        parentBC = childBC.parent;
      } else {
        break;
      }
    }

    let target = null;
    let inProcessBC = null;

    if (parentBC && parentBC.currentWindowGlobal) {
      target = parentBC.currentWindowGlobal.getActor("DOMFullscreen");
      inProcessBC = childBC;
      this.nextMsgRecipient = [target, inProcessBC];
    } else {
      target = this.requestOrigin;
    }

    if (
      !target ||
      target.hasBeenDestroyed() ||
      target.windowContext?.isInBFCache
    ) {
      return [null, null];
    }
    return [target, inProcessBC];
  }

  enterDomFullscreen(aWindow, aBrowser) {
    if (!aWindow.document.fullscreenElement) {
      this.requestOrigin = null;
      return;
    }

    // If it is a remote browser, send a message to ask the content
    // to enter fullscreen state. We don't need to do so if it is an
    // in-process browser, since all related document should have
    // entered fullscreen state at this point.
    // Additionally, in Fission world, we may need to notify the
    // frames in the middle (content frames that embbed the oop iframe where
    // the element requesting fullscreen lives) to enter fullscreen
    // first.
    // This should be done before the active tab check below to ensure
    // that the content document handles the pending request. Doing so
    // before the check is fine since we also check the activeness of
    // the requesting document in content-side handling code.
    if (aWindow.FullScreen._isRemoteBrowser(aBrowser)) {
      // The cached message recipient in actor is used for fullscreen state
      // cleanup, we should not use it while entering fullscreen.
      let [targetActor, inProcessBC] = this._getNextMsgRecipientActor(
        false /* aUseCache */
      );
      if (!targetActor) {
        // If there is no appropriate actor to send the message we have
        // no way to complete the transition and should abort by exiting
        // fullscreen.
        this._abortEnterFullscreen(aWindow);
        return;
      }
      // Record that the actor is waiting for its child to enter
      // fullscreen so that if it dies we can abort.
      targetActor.waitingForChildEnterFullscreen = true;
      targetActor.sendAsyncMessage("DOMFullscreen:Entered", {
        remoteFrameBC: inProcessBC,
      });

      if (inProcessBC) {
        // We aren't messaging the request origin yet, skip this time.
        return;
      }
    }

    // If we've received a fullscreen notification, we have to ensure that the
    // element that's requesting fullscreen belongs to the browser that's currently
    // active. If not, we exit fullscreen since the "full-screen document" isn't
    // actually visible now.
    if (
      !aBrowser ||
      aWindow.gBrowser.selectedBrowser != aBrowser ||
      // The top-level window has lost focus since the request to enter
      // full-screen was made. Cancel full-screen.
      Services.focus.activeWindow != aWindow
    ) {
      this._abortEnterFullscreen(aWindow);
      return;
    }

    aWindow.FullScreen.onEnteredDomFullscreen();
  }

  _abortEnterFullscreen(aWindow) {
    // This function is called synchronously in fullscreen change, so
    // we have to avoid calling exitFullscreen synchronously here.
    //
    // This could reject if we're not currently in fullscreen
    // so just ignore rejection.
    aWindow.setTimeout(
      () => aWindow.document.exitFullscreen().catch(() => {}),
      0
    );
    if (this.timerId) {
      // Cancel the stopwatch for any fullscreen change to avoid
      // errors if it is started again.
      Glean.fullscreen.change.cancel(this.timerId);
      this.timerId = null;
    }
  }

  didDestroy() {
    this._didDestroy = true;

    let window = this._fullscreenWindow;
    if (!window) {
      let topBrowsingContext = this.browsingContext.top;
      let browser = topBrowsingContext.embedderElement;
      if (!browser) {
        return;
      }

      if (
        this.waitingForChildExitFullscreen ||
        this.waitingForChildEnterFullscreen
      ) {
        this.waitingForChildExitFullscreen = false;
        this.waitingForChildEnterFullscreen = false;
        // We were destroyed while waiting for our DOMFullscreenChild to exit
        // or enter fullscreen, run cleanup steps anyway.
        this._cleanupFullscreenStateAndResumeChromeUI(browser.documentGlobal);
      }

      if (this != this.requestOrigin) {
        // The current fullscreen requester should handle the fullsceen event
        // if any.
        this.removeListeners(browser.documentGlobal);
      }
      return;
    }

    if (this.waitingForChildEnterFullscreen) {
      this.waitingForChildEnterFullscreen = false;
      if (window.document.fullscreen) {
        // We were destroyed while waiting for our DOMFullscreenChild
        // to transition to fullscreen so we abort the entire
        // fullscreen transition to prevent getting stuck in a
        // partial fullscreen state. We need to go through the
        // document since window.Fullscreen could be undefined
        // at this point.
        //
        // This could reject if we're not currently in fullscreen
        // so just ignore rejection.
        window.document.exitFullscreen().catch(() => {});
        return;
      }
      this.cleanupDomFullscreen(window);
    }

    // Need to resume Chrome UI if the window is still in fullscreen UI
    // to avoid the window stays in fullscreen problem. (See Bug 1620341)
    if (window.document.documentElement.hasAttribute("inDOMFullscreen")) {
      this.cleanupDomFullscreen(window);
      if (window.windowUtils) {
        window.windowUtils.remoteFrameFullscreenReverted();
      }
    } else if (this.waitingForChildExitFullscreen) {
      this.waitingForChildExitFullscreen = false;
      // We were destroyed while waiting for our DOMFullscreenChild to exit
      // run cleanup steps anyway.
      this._cleanupFullscreenStateAndResumeChromeUI(window);
    }
    this.updateFullscreenWindowReference(window);
  }

  receiveMessage(aMessage) {
    let topBrowsingContext = this.browsingContext.top;
    let browser = topBrowsingContext.embedderElement;

    if (!browser) {
      // No need to go further when the browser is not accessible anymore
      // (which can happen when the tab is closed for instance),
      return;
    }

    let window = browser.documentGlobal;
    switch (aMessage.name) {
      case "DOMFullscreen:Request": {
        const keyboardLockEnabled = Services.prefs.getBoolPref(
          "dom.fullscreen.keyboard_lock.enabled",
          false
        );
        this.fullscreenKeyboardLock = keyboardLockEnabled
          ? aMessage.data.fullscreenKeyboardLock
          : "none";
        this.manager.fullscreen = true;
        this.waitingForChildExitFullscreen = false;
        this.requestOrigin = this;
        this.addListeners(window);
        window.windowUtils.remoteFrameFullscreenChanged(
          browser,
          this.fullscreenKeyboardLock == "browser"
        );
        break;
      }
      case "DOMFullscreen:NewOrigin": {
        // Don't show the warning if we've already exited fullscreen.
        if (window.document.fullscreen) {
          window.PointerlockFsWarning.showFullScreen(
            topBrowsingContext,
            window.document.fullscreenKeyboardLock == "browser"
          );
        }
        this.updateFullscreenWindowReference(window);
        break;
      }
      case "DOMFullscreen:Entered": {
        this.manager.fullscreen = true;
        this.nextMsgRecipient = null;
        this.waitingForChildEnterFullscreen = false;
        this.enterDomFullscreen(window, browser);
        this.updateFullscreenWindowReference(window);
        break;
      }
      case "DOMFullscreen:Exit": {
        this.manager.fullscreen = false;
        this.waitingForChildEnterFullscreen = false;
        window.windowUtils.remoteFrameFullscreenReverted();
        break;
      }
      case "DOMFullscreen:Exited": {
        this.manager.fullscreen = false;
        this.waitingForChildExitFullscreen = false;
        this.cleanupDomFullscreen(window);
        this.updateFullscreenWindowReference(window);
        break;
      }
      case "DOMFullscreen:Painted": {
        this.waitingForChildExitFullscreen = false;
        Services.obs.notifyObservers(window, "fullscreen-painted");
        this.sendAsyncMessage("DOMFullscreen:Painted", {});
        Glean.fullscreen.change.stopAndAccumulate(this.timerId);
        this.timerId = null;
        break;
      }
      case "DOMFullscreen:UpdateKeyboardLock": {
        // Validate the received keyboardlock state before updating - an
        // infected content process could send something unexpected.
        const keyboardLockEnabled = Services.prefs.getBoolPref(
          "dom.fullscreen.keyboard_lock.enabled",
          false
        );
        let newLock =
          keyboardLockEnabled &&
          (aMessage.data.fullscreenKeyboardLock == "none" ||
            aMessage.data.fullscreenKeyboardLock == "browser")
            ? aMessage.data.fullscreenKeyboardLock
            : "none";
        if (window.document.fullscreenKeyboardLock != newLock) {
          this.manager.updateFullscreenKeyboardLockStatus(newLock);
          window.PointerlockFsWarning.close("fullscreen-warning");
          window.PointerlockFsWarning.showFullScreen(
            this.browsingContext,
            newLock == "browser"
          );
        }
        break;
      }
    }
  }

  handleEvent(aEvent) {
    let window = aEvent.currentTarget;
    // We can not get the corresponding browsing context from actor if the actor
    // has already destroyed, so use event target to get browsing context
    // instead.
    let requestOrigin = window.browsingContext.fullscreenRequestOrigin?.get();
    if (this != requestOrigin) {
      // The current fullscreen requester should handle the fullsceen event,
      // ignore them if we are not the current requester.
      this.removeListeners(window);
      return;
    }

    switch (aEvent.type) {
      case "MozDOMFullscreen:Entered": {
        // The event target is the element which requested the DOM
        // fullscreen. If we were entering DOM fullscreen for a remote
        // browser, the target would be the browser which was the parameter of
        // `remoteFrameFullscreenChanged` call. If the fullscreen
        // request was initiated from an in-process browser, we need
        // to get its corresponding browser here.
        let browser;
        if (aEvent.target.documentGlobal == window) {
          browser = aEvent.target;
        } else {
          browser = aEvent.target.documentGlobal.docShell.chromeEventHandler;
        }

        // Addon installation should be cancelled when entering fullscreen for security and usability reasons.
        // Installation prompts in fullscreen can trick the user into installing unwanted addons.
        // In fullscreen the notification box does not have a clear visual association with its parent anymore.
        if (window.gXPInstallObserver) {
          window.gXPInstallObserver.removeAllNotifications(browser);
        }

        this.timerId = Glean.fullscreen.change.start();
        this.enterDomFullscreen(window, browser);
        this.updateFullscreenWindowReference(window);

        if (!this.hasBeenDestroyed() && this.requestOrigin) {
          window.PointerlockFsWarning.showFullScreen(
            this.requestOrigin.browsingContext,
            browser.documentGlobal.document.fullscreenKeyboardLock == "browser"
          );
        }
        break;
      }
      case "MozDOMFullscreen:Exited": {
        this.timerId = Glean.fullscreen.change.start();

        // Make sure that the actor has not been destroyed before
        // accessing its browsing context. Otherwise, a error may
        // occur and hence cleanupDomFullscreen not executed, resulting
        // in the browser window being in an unstable state.
        // (Bug 1590138).
        if (!this.hasBeenDestroyed() && !this.requestOrigin) {
          this.requestOrigin = this;
        }
        this.cleanupDomFullscreen(window);
        this.updateFullscreenWindowReference(window);

        // If the document is supposed to be in fullscreen, keep the listener to wait for
        // further events.
        if (!this.manager.fullscreen) {
          this.removeListeners(window);
        }
        break;
      }
      case "MozDOMFullscreen:WarnAboutKeyboardLock": {
        if (!this.hasBeenDestroyed() && this.requestOrigin) {
          window.PointerlockFsWarning.showFullScreen(
            this.requestOrigin.browsingContext,
            window.document.fullscreenKeyboardLock == "browser"
          );
        }
        break;
      }
    }
  }

  addListeners(aWindow) {
    aWindow.addEventListener(
      "MozDOMFullscreen:Entered",
      this,
      /* useCapture */ true,
      /* wantsUntrusted */
      false
    );
    aWindow.addEventListener(
      "MozDOMFullscreen:Exited",
      this,
      /* useCapture */ true,
      /* wantsUntrusted */ false
    );
    aWindow.addEventListener(
      "MozDOMFullscreen:WarnAboutKeyboardLock",
      this,
      /* useCapture */ true,
      /* wantsUntrusted */ false
    );
  }

  removeListeners(aWindow) {
    aWindow.removeEventListener("MozDOMFullscreen:Entered", this, true);
    aWindow.removeEventListener("MozDOMFullscreen:Exited", this, true);
    aWindow.removeEventListener(
      "MozDOMFullscreen:WarnAboutKeyboardLock",
      this,
      true
    );
  }

  /**
   * Get the actor where the original fullscreen
   * enter or exit request comes from.
   */
  get requestOrigin() {
    let chromeBC = this.browsingContext.topChromeWindow?.browsingContext;
    let requestOrigin = chromeBC?.fullscreenRequestOrigin;
    return requestOrigin && requestOrigin.get();
  }

  /**
   * Store the actor where the original fullscreen
   * enter or exit request comes from in the top level
   * browsing context.
   */
  set requestOrigin(aActor) {
    let chromeBC = this.browsingContext.topChromeWindow?.browsingContext;
    if (!chromeBC) {
      console.error("not able to get browsingContext for chrome window.");
      return;
    }

    if (aActor) {
      chromeBC.fullscreenRequestOrigin = Cu.getWeakReference(aActor);
    } else {
      delete chromeBC.fullscreenRequestOrigin;
    }
  }

  hasBeenDestroyed() {
    if (this._didDestroy) {
      return true;
    }

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
