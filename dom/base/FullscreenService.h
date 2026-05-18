/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_FullscreenService_h
#define mozilla_dom_FullscreenService_h

#include "mozilla/Logging.h"
#include "mozilla/Maybe.h"
#include "mozilla/dom/BrowsingContext.h"
#include "nsGlobalWindowOuter.h"
#include "nsIFullscreenService.h"
#include "nsIObserver.h"
#include "nsLiteralString.h"
#include "nsTArray.h"

namespace mozilla::dom {

struct FullscreenOptions;

class CanonicalBrowsingContext;
class Document;
class Element;
class FullscreenManager;
class WindowGlobalParent;

class FullscreenService final : public nsIFullscreenService,
                                public nsIObserver {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIFULLSCREENSERVICE
  NS_DECL_NSIOBSERVER
  // Constructor
  static already_AddRefed<FullscreenService> GetSingleton();

  static FullscreenService* Get();

  static LogModule* GetLogModule();

  static bool Enabled();

  static constexpr auto sEnteredFullscreenEvent =
      u"MozDOMFullscreen:Entered"_ns;
  static constexpr auto sExitedFullscreenEvent = u"MozDOMFullscreen:Exited"_ns;

  // called during XPCOM will shut down phase
  void Shutdown();

  using EnterResolve = std::function<void(nsresult)>;
  using ExitResolve = EnterResolve;

  static void SendRequestFullscreen(RefPtr<Element> aElement,
                                    RefPtr<Promise> aPromise,
                                    const FullscreenOptions& aOptions,
                                    CallerType aCallerType);
  static void SendRequestExitFullscreen(Document* aDocument, Promise* aPromise);

  static void RecvRequestFullscreen(CanonicalBrowsingContext* aContext,
                                    uint64_t aChildRequestId,
                                    bool aKeyboardLock,
                                    EnterResolve&& aResolve);

  static void RecvRequestExitFullscreen(CanonicalBrowsingContext* aContext,
                                        uint64_t aChildRequestId,
                                        ExitResolve&& aResolve);

  // Called when the originating WindowGlobal sends FullscreenServiceTransaction
  // to report success / failure of its post-resolver apply (or restore)
  // step. Routes to the currently running EnterFullscreen / ExitFullscreen
  // after validating aChildRequestId. Works like a "tick" on the Fullscreen
  // Service queue.
  static void ReceivedFullscreenTransaction(
      const MaybeDiscardedBrowsingContext& aContext, uint64_t aChildRequestId,
      nsresult aResult);

  // Called by FinishDOMFullscreenChange and is our hook
  // into fullscreen transitions.
  static void FullscreenChanged(uint64_t aWindowId, Document* aChromeDoc,
                                bool aIsEnteringFullscreen);

  // Notify UI that we entered fullscreen by sending MozDOMFullscreen:Entered
  MOZ_CAN_RUN_SCRIPT_BOUNDARY static void DispatchEnteredFullscreenChromeEvent(
      Document* aDoc, CanonicalBrowsingContext* aContext);

  // Notify UI that we entered fullscreen by sending MozDOMFullscreen:Exited
  MOZ_CAN_RUN_SCRIPT_BOUNDARY static void DispatchExitedFullscreenChromeEvent(
      nsGlobalWindowOuter* aWindow);

  FullscreenManager* Manager(uint64_t aWindowId);
  FullscreenManager* GetManager(uint64_t aWindowId);

  // Called from ~nsGlobalWindowOuter to reap the per-window FullscreenManager.
  static void OnWindowOuterDestroyed(uint64_t aWindowId);
 private:
  nsTArray<UniquePtr<FullscreenManager>> mManagers;

  FullscreenService();
  // A private destructor must be declared. Defined out-of-line so that
  // UniquePtr<FullscreenManager> is only destroyed in a TU where
  // FullscreenManager is a complete type.
  ~FullscreenService();
};

// Tri-state for a document and whether or not it is
// Simple fullscreen doc - Some(true)
// Non-simple fullscreen doc - Some(false)
// Not a fullscreen doc - Nothing()
using FullscreenSimpleDoc = Maybe<bool>;

}  // namespace mozilla::dom

#define FULLSCREEN_LOG(FMT_STRING, ...)                                \
  MOZ_LOG_FMT(dom::FullscreenService::GetLogModule(), LogLevel::Debug, \
              FMT_STRING __VA_OPT__(, ) __VA_ARGS__)

#endif  // mozilla_FullscreenService_h
