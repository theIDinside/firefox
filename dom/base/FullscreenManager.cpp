/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FullscreenManager.h"

#include <algorithm>

#include "mozilla/Assertions.h"
#include "mozilla/StaticPrefs_full_screen_api.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/FullscreenPaintBarrier.h"
#include "mozilla/dom/FullscreenService.h"
#include "mozilla/dom/WindowGlobalParent.h"

namespace mozilla::dom {

FullscreenServiceRequest::FullscreenServiceRequest(
    FullscreenManager* aManager, CanonicalBrowsingContext* aContext,
    uint64_t aTransactionId)
    : mManager(aManager),
      mBrowsingContextId(aContext->Id()),
      mTransactionId(aTransactionId) {}

RefPtr<CanonicalBrowsingContext> FullscreenServiceRequest::GetBrowsingContext()
    const {
  return CanonicalBrowsingContext::Get(mBrowsingContextId);
}

void FullscreenServiceRequest::SetRunning(bool aIsRunning) {
  mRunning = aIsRunning;
  if (mTimer) {
    mTimer->Cancel();
    mTimer = nullptr;
  }
}

void FullscreenServiceRequest::InitRequestTimer() {
  const auto timeout = StaticPrefs::full_screen_api_request_timeout_ms();
  if (timeout != 0) {
    nsCOMPtr<nsIObserver> observer = new FullscreenServiceTimerObserver(this);
    NS_NewTimerWithObserver(getter_AddRefs(mTimer), observer, timeout,
                            nsITimer::TYPE_ONE_SHOT);
  }
}

NS_IMETHODIMP FullscreenServiceTimerObserver::GetName(nsACString& aName) {
  aName.AssignLiteral("FullscreenServiceTimerObserver");
  return NS_OK;
}

NS_IMPL_ISUPPORTS(FullscreenServiceTimerObserver, nsIObserver, nsINamed)

NS_IMETHODIMP FullscreenServiceTimerObserver::Observe(nsISupports* aSubject,
                                                      const char* aTopic,
                                                      const char16_t* aData) {
  MOZ_LOG_FMT(FullscreenService::GetLogModule(), LogLevel::Debug,
              "Fullscreen timer fired.");

  if (mPayload->IsRunning()) {
    mPayload->Cancel();
  }

  return NS_OK;
}

FullscreenServiceTimerObserver::FullscreenServiceTimerObserver(
    FullscreenServiceRequest* aRequestPayload)
    : mPayload(aRequestPayload) {}

FullscreenServiceTimerObserver::~FullscreenServiceTimerObserver() = default;

FullscreenServiceRequest::~FullscreenServiceRequest() = default;

static Element* GetTopLevelEmbedder(BrowserParent* aBrowserParent) {
  if (!aBrowserParent || aBrowserParent->IsDestroyed()) {
    return nullptr;
  }
  CanonicalBrowsingContext* topBC = aBrowserParent->GetBrowsingContext()->Top();
  if (!topBC) {
    return nullptr;
  }

  if (topBC->IsDiscarded()) {
    return nullptr;
  }

  return topBC->GetEmbedderElement();
}

bool EnterFullscreen::RequestFullscreenTransition() {
  RefPtr<CanonicalBrowsingContext> browsingContext = GetBrowsingContext();
  BrowserParent* parent =
      browsingContext ? browsingContext->GetBrowserParent() : nullptr;
  Element* embedderElement = GetTopLevelEmbedder(parent);

  if (!embedderElement) {
    return false;
  }

  auto* window = browsingContext->GetTopCrossChromeBoundaryDOMWindow();

  if (!window) {
    return false;
  }

  auto* ownerDoc = embedderElement->GetOwnerDocument();
  MOZ_ASSERT(ownerDoc, "Could not get owner document");
  // Begin step 8 of enter fullscreen algorithm
  // https://fullscreen.spec.whatwg.org/#dom-element-requestfullscreen
  if (!ownerDoc->Fullscreen() && !window->GetFullScreen()) {
    mManager->SetFullscreen(FullscreenReason::ForFullscreenAPI,
                            /* aIsFullscreen */ true);
  }
  return true;
}

void EnterFullscreen::EnterFullscreenInContentProcesses() {
  RefPtr<CanonicalBrowsingContext> bc = GetBrowsingContext();
  BrowserParent* browserParent = bc ? bc->GetBrowserParent() : nullptr;
  if (!browserParent || browserParent->IsDestroyed()) {
    Cancel();
    return;
  }

  // TODO: Add optimization for EnterFullscreen where it verifies up front, that
  // any fullscreen changes that can happen, will only be local to it's own
  // process. Then we don't have to ask any potential OOP frames here.

  nsTArray<RefPtr<BrowserParent::RequestRemoteFrameApplyFullscreenPromise>>
      remoteFramesPromises;

  auto context = browserParent->GetBrowsingContext();

  while (context) {
    auto* parentOfContext = context->GetParent();
    auto* browserParent =
        parentOfContext ? parentOfContext->GetBrowserParent() : nullptr;
    if (browserParent && browserParent->GetBrowsingContext()) {
      remoteFramesPromises.AppendElement(
          browserParent->RequestFullscreenForRemoteFrame(context));
      // Get the "top-most browsing context in the content process tree
      // fragment", and continue iteration from there
      context = browserParent->GetBrowsingContext();
      continue;
    }
    break;
  }

  auto allRemoteFramesNotified =
      BrowserParent::RequestRemoteFrameApplyFullscreenPromise::All(
          GetMainThreadSerialEventTarget(), remoteFramesPromises);

  using Result = BrowserParent::RequestRemoteFrameApplyFullscreenPromise::
      AllPromiseType::ResolveOrRejectValue;

  const auto onResponse = [self = RefPtr{this}](Result&& aResponses) {
    // Already cancelled by something.
    if (!self->IsRunning()) {
      FULLSCREEN_LOG("Enter fullscreen request was aborted.");
      return;
    }

    // If resolved and all were NS_OK
    using std::ranges::all_of;
    const bool allSuccess =
        aResponses.IsResolve()
            ? all_of(aResponses.ResolveValue(),
                     [](auto value) { return NS_SUCCEEDED(value); })
            : false;
    if (allSuccess) {
      self->Commit();
    } else {
      self->Cancel();
    }
  };

  if (remoteFramesPromises.IsEmpty()) {
    Commit();
  } else {
    allRemoteFramesNotified->Then(GetCurrentSerialEventTarget(), __func__,
                                  std::move(onResponse));
  }
}

void EnterFullscreen::Resolve(nsresult aResult) {
  if (mResolve) {
    auto resolve = std::move(mResolve);
    mResolve = nullptr;
    resolve(aResult);
  }
}

void EnterFullscreen::Commit() {
  RefPtr<CanonicalBrowsingContext> context = GetBrowsingContext();
  mManager->SetFullscreenLeaf(context);
  if (auto* winContext = context->GetCurrentWindowGlobal()) {
    winContext->SetFullscreen(true);
    winContext->UpdateFullscreenKeyboardLockStatus(
        mKeyboardLock ? FullscreenKeyboardLock::Browser
                      : FullscreenKeyboardLock::None);
  }
  Resolve(NS_OK);
}

void EnterFullscreen::OnTransactionError() {
  FULLSCREEN_LOG("EnterFullscreen::OnTransactionError");
  MOZ_ASSERT(!mAwaitingChromeExit);

  // We must take ourselves out of browser fullscreen
  mAwaitingChromeExit = true;
  mManager->CancelFullscreen();
  nsGlobalWindowOuter* window = mManager->GetWindow();
  if (!window || !window->GetFullScreen()) {
    // Not in browser fullscreen, start next request.
    mManager->OnRequestComplete(true);
  }
}

void EnterFullscreen::Cancel() {
  Resolve(NS_ERROR_ABORT);
  if (!IsRunning()) {
    return;
  }
  // Service initiated abort
  OnTransactionError();
}

void EnterFullscreen::Run() {
  InitRequestTimer();
  if (!mManager->GetWindow()->Fullscreen()) {
    if (!RequestFullscreenTransition()) {
      Cancel();
    }
  } else {
    EnterFullscreenInContentProcesses();
  }
}

void EnterFullscreen::OnFullscreenChange(Document* aChromeDoc,
                                         bool aEnteredFullscreen) {
  FULLSCREEN_LOG("EnterFullscreen::OnFullscreenChange, entered={}",
                 aEnteredFullscreen);

  if (mAwaitingChromeExit) {
    if (!aEnteredFullscreen) {
      mManager->OnRequestComplete(true);
    }
    return;
  }

  RefPtr<CanonicalBrowsingContext> bc = GetBrowsingContext();
  BrowserParent* browserParent = bc ? bc->GetBrowserParent() : nullptr;

  if (!browserParent || browserParent->IsDestroyed() || !aEnteredFullscreen) {
    FULLSCREEN_LOG("EnterFullscreen::OnFullscreenChange cancel fullscreen");
    mManager->CancelFullscreen();
    return;
  }

  if (!aChromeDoc->Fullscreen()) {
    Element* chromeDocEmbedElement = GetTopLevelEmbedder(browserParent);
    aChromeDoc->ApplyFullscreen(chromeDocEmbedElement);
    FullscreenService::DispatchEnteredFullscreenChromeEvent(aChromeDoc, bc);
  }

  EnterFullscreenInContentProcesses();
}

template <typename Fn>
static void ForEachPBrowser(CanonicalBrowsingContext* aFromContext,
                            CanonicalBrowsingContext* aToContext, Fn&& fn) {
  // For each unique PBrowser in aFromContext .. aToContext do `fn`
  AutoTArray<BrowserParent*, 16> visitedBrowserParents;
  while (aFromContext) {
    if (NS_WARN_IF(aFromContext->IsChrome())) {
      break;
    }
    BrowserParent* bp = aFromContext->GetBrowserParent();
    if (bp && !bp->IsDestroyed() && !visitedBrowserParents.Contains(bp)) {
      visitedBrowserParents.AppendElement(bp);
      fn(bp);
    }
    if (aFromContext == aToContext) {
      break;
    }
    aFromContext = aFromContext->GetParent();
  }
}

void ExitFullscreen::ExitBrowserFullscreen() {
  RefPtr<CanonicalBrowsingContext> browsingContext = GetBrowsingContext();
  BrowserParent* parent =
      browsingContext ? browsingContext->GetBrowserParent() : nullptr;
  Element* embedderElement = GetTopLevelEmbedder(parent);

  if (!embedderElement) {
    return;
  }

  auto* window = browsingContext->GetTopCrossChromeBoundaryDOMWindow();

  if (!window) {
    return;
  }

  auto* ownerDoc = embedderElement->GetOwnerDocument();
  MOZ_ASSERT(ownerDoc, "Could not get owner document");
  if (ownerDoc->Fullscreen() && window->GetFullScreen()) {
    mRequestedExit = true;
    mManager->SetFullscreen(FullscreenReason::ForFullscreenAPI,
                            /* aIsFullscreen */ false);
    return;
  }
}

void ExitFullscreen::DispatchContentProcessWork(
    CanonicalBrowsingContext* aTopContext) {
  AutoTArray<BrowserParent*, 16> remotes;
  RefPtr<CanonicalBrowsingContext> leafContext = mManager->GetFullscreenLeaf();

  RefPtr<CanonicalBrowsingContext> reqContext = GetBrowsingContext();
  BrowserParent* reqBp = reqContext->GetBrowserParent();
  BrowserParent* topParent =
      aTopContext ? aTopContext->GetBrowserParent() : nullptr;
  // If these are the same, topParent document tree is processed in callback
  if (topParent == reqBp) {
    topParent = nullptr;
  }

  ForEachPBrowser(leafContext, aTopContext, [&](BrowserParent* aBp) {
    MOZ_ASSERT(!remotes.Contains(aBp));
    // Requesting bp context is handled by service request callback
    // topParent bp context shall not be exited fully, if topParent != nullptr.
    if (aBp != reqBp && aBp != topParent) {
      remotes.AppendElement(aBp);
    }
  });

  FULLSCREEN_LOG("Remotes count={}", remotes.Length());

  // All PBrowser document trees below request shall exit fully.
  // All PBrowser document trees with simple fullscreen docs shall exit fully
  // (if they are not the PBrowser document tree making the request)
  for (const auto& bp : remotes) {
    (void)bp->SendExitFullscreenFullyForRemoteFrame();
  }

  if (topParent) {
    // topParent contains a non-simple fs document in its' doc tree.
    (void)topParent->SendExitFullscreenInRemote(aTopContext);
  }

  mManager->SetFullscreenLeaf(aTopContext);
  Resolve(NS_OK);
}

static bool IsSameOrAncestorOf(CanonicalBrowsingContext* aAncestor,
                               CanonicalBrowsingContext* aChild) {
  if (!aChild || !aAncestor) {
    return false;
  }
  do {
    if (aChild == aAncestor) {
      return true;
    }
  } while ((aChild = aChild->GetParentCrossChromeBoundary()));
  return false;
}

void ExitFullscreen::Run() {
  InitRequestTimer();

  nsTArray<RefPtr<BrowserParent::CollectFullscreenDocsToUnfullscreenPromise>>
      checkPromises;

  RefPtr<CanonicalBrowsingContext> leafContext = mManager->GetFullscreenLeaf();
  RefPtr<CanonicalBrowsingContext> requestingContext = GetBrowsingContext();

  const bool contextNoLongerInFullscreen =
      (!leafContext || IsSameOrAncestorOf(leafContext, requestingContext)) &&
      leafContext != requestingContext;

  if (contextNoLongerInFullscreen) {
    // INVALID_ARG to signal that content process should resolve with undefined,
    // because effectively it's fullscreenElement is null.
    Resolve(nsresult::NS_ERROR_INVALID_ARG);
    return;
  }

  // Do SendCollectFullscreenDocsToUnfullscreen for all PBrowser pairs
  // starting at leafContext.
  AutoTArray<BrowserParent*, 16> visitedBrowserParents;
  ForEachPBrowser(leafContext, nullptr, [&](BrowserParent* aBp) {
    FULLSCREEN_LOG(
        "SendCollectFullscreenDocsToUnfullscreen browserParent={}, "
        "browsingContext={}, bp={}",
        aBp->Id(), leafContext->Id(), (void*)aBp);
    checkPromises.AppendElement(aBp->SendCollectFullscreenDocsToUnfullscreen());
  });

  using Result = BrowserParent::CollectFullscreenDocsToUnfullscreenPromise::
      AllPromiseType::ResolveOrRejectValue;

  const auto onResponse = [self = RefPtr{this}](Result&& aResponse) {
    if (!aResponse.IsResolve()) {
      FULLSCREEN_LOG("ExitFullscreen::Run: Cancel");
      self->Cancel();
      return;
    }

    RefPtr<CanonicalBrowsingContext> requestContext =
        self->GetBrowsingContext();

    AutoTArray<FullscreenDocStatus, 16> flattened;
    CanonicalBrowsingContext* topContextRemainingInFullscreen = nullptr;

    bool allSimple = true;

    for (const auto& subTree : aResponse.ResolveValue()) {
      if (!allSimple) {
        break;
      }
      for (const FullscreenDocStatus& status : subTree) {
        if (status.browsingContext().IsNullOrDiscarded() ||
            status.isSimple().isNothing()) {
          self->Cancel();
          return;
        }
        const bool isNonSimpleDocAncestor =
            !status.isSimple().value() &&
            IsSameOrAncestorOf(status.browsingContext()->Canonical(),
                               requestContext);
        if (isNonSimpleDocAncestor) {
          topContextRemainingInFullscreen =
              status.browsingContext()->Canonical();
          allSimple = false;
          break;
        }
      }
    }

    if (allSimple) {
      FULLSCREEN_LOG("All were simple, exiting all");
      self->ExitBrowserFullscreen();
    } else {
      FULLSCREEN_LOG("Non-simple doc found.");
      self->DispatchContentProcessWork(topContextRemainingInFullscreen);
    }
  };

  BrowserParent::CollectFullscreenDocsToUnfullscreenPromise::All(
      GetMainThreadSerialEventTarget(), checkPromises)
      ->Then(GetCurrentSerialEventTarget(), __func__, std::move(onResponse));
}

void ExitFullscreen::OnFullscreenChange(Document* aChromeDoc,
                                        bool aEnteredFullscreen) {
  FULLSCREEN_LOG(
      "ExitFullscreen::OnFullscreenChange, aChromeDoc={:p}, entered "
      "fullscreen={}",
      (void*)aChromeDoc, aEnteredFullscreen);
  if (mAwaitingChromeExit) {
    if (!aEnteredFullscreen) {
      mManager->OnRequestComplete(true);
    }
    return;
  }
  if (!mRequestedExit) {
    FULLSCREEN_LOG("ExitFullscreen::OnFullscreenChange cancel fullscreen");
    mManager->CancelFullscreen();
    return;
  }
  Document::ExitFullscreenInDocTree(aChromeDoc);
  DispatchContentProcessWork(nullptr);
}

void ExitFullscreen::Resolve(nsresult aStatus) {
  if (mResolve) {
    auto resolve = std::move(mResolve);
    mResolve = nullptr;
    resolve(aStatus);
  }
}

void ExitFullscreen::OnTransactionError() {
  FULLSCREEN_LOG("ExitFullscreen::OnTransactionError");

  // Failure path. If chrome is mid-exit (or any FS transition), wait for
  // OnFullscreenChange before letting the queue advance. Otherwise finalize
  // now.
  mAwaitingChromeExit = true;
  nsGlobalWindowOuter* window = mManager->GetWindow();
  if (!window || !window->GetFullScreen()) {
    mManager->OnRequestComplete(true);
  }
}

void ExitFullscreen::Cancel() {
  Resolve(NS_ERROR_ABORT);
  if (!IsRunning()) {
    return;
  }
  // Single failure path. Same contract as EnterFullscreen::Cancel: fire
  // mResolve once (best-effort, rejects the JS-side promise) and route
  // finalization through OnServiceTransactionComplete so chrome FS exit is
  // awaited if necessary.
  OnTransactionError();
}

FullscreenManager::FullscreenManager(uint64_t aWindowId)
    : mWindowId(aWindowId) {}

FullscreenManager::~FullscreenManager() {
  // Break the request <-> timer <-> observer cycle so the timer doesn't fire
  // into a freed manager. SetRunning(false) cancels the timer, which drops the
  // observer's strong ref back to the request. Queued (non-current) requests
  // never armed a timer.
  if (mCurrentRequest) {
    mCurrentRequest->Cancel();
  }
}

void FullscreenManager::QueueRequest(
    RefPtr<FullscreenServiceRequest> aRequest) {
  mRequests.AppendElement(std::move(aRequest));
  StartNextRequest();
}

void FullscreenManager::QueueEnterRequest(
    CanonicalBrowsingContext* aContext, bool aKeyboardLock,
    uint64_t aChildRequestId, EnterFullscreen::Resolver&& aResolve) {
  FULLSCREEN_LOG("Queue enter request, id={}, bc={}", aChildRequestId,
                 aContext->Id());
  QueueRequest(MakeRefPtr<EnterFullscreen>(
      this, aContext, aKeyboardLock, aChildRequestId, std::move(aResolve)));
}

void FullscreenManager::QueueExitRequest(CanonicalBrowsingContext* aContext,
                                         uint64_t aChildRequestId,
                                         ExitFullscreen::Resolver&& aResolve) {
  FULLSCREEN_LOG("Queue exit request, id={}, bc={}", aChildRequestId,
                 aContext->Id());
  QueueRequest(MakeRefPtr<ExitFullscreen>(this, aContext, aChildRequestId,
                                          std::move(aResolve)));
}

void FullscreenManager::OnFullscreenChanged(Document* aChromeDoc,
                                            bool aIsEnteringFullscreen) {
  FULLSCREEN_LOG(
      "FullscreenManager::OnFullscreenChanged, chrome doc={:p}, DOM "
      "Fullscreen={}",
      (void*)aChromeDoc, aIsEnteringFullscreen);

  const bool isExitingHasFullscreenStateNoOngoingRequest =
      !aIsEnteringFullscreen && mFullscreenedBrowsingContextLeaf.isSome() &&
      !mCurrentRequest;

  if (!aIsEnteringFullscreen) {
    FullscreenService::DispatchExitedFullscreenChromeEvent(GetWindow());
  }

  if (isExitingHasFullscreenStateNoOngoingRequest) {
    FULLSCREEN_LOG("FullscreenManager::OnFullscreenChanged cancel fullscreen");
    CancelFullscreen();
    return;
  }

  if (!mCurrentRequest) {
    // We waited to start a request because there was an ongoing fs transition.
    // Start requests if there are any.
    StartNextRequest();
    return;
  }

  mCurrentRequest->OnFullscreenChange(aChromeDoc, aIsEnteringFullscreen);
}

void FullscreenManager::OnRequestComplete(bool aStartNext) {
  if (mCurrentRequest) {
    mCurrentRequest->SetRunning(false);
    mCurrentRequest = nullptr;
  }

  if (aStartNext) {
    StartNextRequest();
  }
}

void FullscreenManager::OnTransactionResponse(
    CanonicalBrowsingContext* aContext, uint64_t aChildRequestId,
    nsresult aResult) {
  if (!mCurrentRequest) {
    FULLSCREEN_LOG("OnServiceTransaction dropped: no current request");
    return;
  }

  if (mCurrentRequest->TransactionId() != aChildRequestId) {
    FULLSCREEN_LOG(
        "OnServiceTransaction dropped: stale request id (got {}, expected {})",
        aChildRequestId, mCurrentRequest->TransactionId());
    return;
  }

  if (NS_SUCCEEDED(aResult)) {
    OnRequestComplete(true);
  } else {
    mCurrentRequest->OnTransactionError();
  }
}

void FullscreenManager::SetOngoingRequest() {
  MOZ_ASSERT(!mRequests.IsEmpty() && mCurrentRequest == nullptr);
  mCurrentRequest = std::move(mRequests[0]);
  mRequests.RemoveElementAt(0);
  // The request is now in-flight. Mark it running so the timer-on-cancel
  // path actually fires.
  mCurrentRequest->SetRunning(true);
}

void FullscreenManager::SetFullscreenLeaf(CanonicalBrowsingContext* aContext) {
  if (aContext) {
    mFullscreenedBrowsingContextLeaf = Some(aContext->Id());
  } else {
    mFullscreenedBrowsingContextLeaf = Nothing();
  }
}

nsGlobalWindowOuter* FullscreenManager::GetWindow() const {
  return nsGlobalWindowOuter::GetOuterWindowWithId(Id());
}

RefPtr<CanonicalBrowsingContext> FullscreenManager::GetFullscreenLeaf() const {
  return mFullscreenedBrowsingContextLeaf
      .map([](auto id) -> RefPtr<CanonicalBrowsingContext> {
        return CanonicalBrowsingContext::Get(id);
      })
      .valueOr(nullptr);
}

void FullscreenManager::SetFullscreen(nsPIDOMWindowOuter* aWindow,
                                      FullscreenReason aReason,
                                      bool aIsFullscreen) {
  if (NS_WARN_IF(!aWindow)) {
    return;
  }

  aWindow->SetFullscreenInternal(aReason, aIsFullscreen);
}

void FullscreenManager::SetFullscreen(FullscreenReason aReason,
                                      bool aIsFullscreen) {
  nsGlobalWindowOuter* window =
      nsGlobalWindowOuter::GetOuterWindowWithId(mWindowId);
  SetFullscreen(window, aReason, aIsFullscreen);
}

void FullscreenManager::StartNextRequest() {
  if (mRequests.IsEmpty() || mCurrentRequest) {
    return;
  }

  // Wait until current transition completes. When it does, OnFullscreenChange
  // will get called and we'll start.
  if (auto window = GetWindow(); window->InProcessFullscreenRequest()) {
    return;
  }

  SetOngoingRequest();
  mCurrentRequest->Run();
}

void FullscreenManager::CancelFullscreen() {
  if (mFullscreenedBrowsingContextLeaf.isNothing()) {
    FULLSCREEN_LOG(
        "FullscreenManager::CancelFullscreen no DOM Fullscreen state");
    return;
  }

  for (auto& req : mRequests) {
    req->Cancel();
  }

  mRequests.Clear();

  if (mCurrentRequest) {
    mCurrentRequest->Resolve(NS_ERROR_ABORT);
    mCurrentRequest->SetRunning(false);
    mCurrentRequest = nullptr;
  }

  FULLSCREEN_LOG(
      "FullscreenManager::CancelFullscreen, leaf DOM Fullscreen browsing "
      "context={}",
      mFullscreenedBrowsingContextLeaf.value());
  nsGlobalWindowOuter* window = GetWindow();
  // Maintain parity with legacy implementation, that can fire manual
  // "fullscreen-painted" notifications Fullscreen Service at least uses
  // MozAfterPaint on the chrome doc
  mozilla::dom::FullscreenPaintBarrier::ArmForDocument(window->GetDocument(),
                                                       /* aIsEnter = */ false);
  Document::ExitFullscreenInDocTree(window->GetDocument());

  RefPtr<CanonicalBrowsingContext> context = GetFullscreenLeaf();
  AutoTArray<BrowserParent*, 16> visitedBrowserParents;
  while (context) {
    if (auto* winContext = context->GetCurrentWindowGlobal()) {
      winContext->SetFullscreen(/* aFullscreen */ false);
    }

    if (NS_WARN_IF(context->IsChrome())) {
      return;
    }

    BrowserParent* bp = context->GetBrowserParent();
    if (bp && !visitedBrowserParents.Contains(bp) && !bp->IsDestroyed()) {
      FULLSCREEN_LOG(
          "SendExitFullscreenFullyForRemoteFrame browserParent={}, "
          "browsingContext={}",
          bp->Id(), context->Id());
      (void)bp->SendExitFullscreenFullyForRemoteFrame();
      visitedBrowserParents.AppendElement(bp);
    }
    context = context->GetParent();
  }

  if (window->GetFullScreen()) {
    SetFullscreen(FullscreenReason::ForForceExitFullscreen,
                  /* aIsFullscreen */ false);
  }
  mFullscreenedBrowsingContextLeaf.reset();
}

}  // namespace mozilla::dom
