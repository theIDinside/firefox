/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FullscreenService.h"

#include "CustomEvent.h"
#include "FullscreenManager.h"
#include "FullscreenPaintBarrier.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/FullscreenChange.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_full_screen_api.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "nsIObserverService.h"

namespace mozilla::dom {

NS_INTERFACE_MAP_BEGIN(FullscreenService)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIObserver)
  NS_INTERFACE_MAP_ENTRY(nsIObserver)
  NS_INTERFACE_MAP_ENTRY(nsIFullscreenService)
NS_INTERFACE_MAP_END

NS_IMPL_ADDREF(FullscreenService)
NS_IMPL_RELEASE(FullscreenService)

static mozilla::StaticRefPtr<FullscreenService> gFullscreenService;
static bool sIsXPCOMShutdown = false;

FullscreenService::~FullscreenService() = default;

FullscreenService::FullscreenService() {
  MOZ_ASSERT(XRE_IsParentProcess());
  FULLSCREEN_LOG("Created fullscreen service");
  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  MOZ_ASSERT(obs);
  DebugOnly<bool> ok = NS_SUCCEEDED(
      obs->AddObserver(this, NS_XPCOM_WILL_SHUTDOWN_OBSERVER_ID, false));
  MOZ_ASSERT(ok);
}

void FullscreenService::Shutdown() {
  FULLSCREEN_LOG("fullscreen service xpcom shutdown");

  gFullscreenService = nullptr;
  sIsXPCOMShutdown = true;
}

/* static */
void FullscreenService::SendRequestFullscreen(RefPtr<Element> aElement,
                                              RefPtr<Promise> aPromise,
                                              const FullscreenOptions& aOptions,
                                              CallerType aCallerType) {
  RefPtr<Document> ownerDoc = aElement->OwnerDoc();
  WindowGlobalChild* wgc = ownerDoc->GetWindowGlobalChild();
  if (!wgc) {
    FullscreenRequest::Reject(ownerDoc, aElement, aPromise);
    return;
  }

  const uint64_t requestId = wgc->NewFullscreenTransactionId();
  RefPtr<BrowsingContext> bc = ownerDoc->GetBrowsingContext();

  auto onResolve = [aPromise, aOptions, aElement, originDoc = ownerDoc,
                    aCallerType, requestId, bc = RefPtr{bc},
                    wgc = RefPtr{wgc}](nsresult aResult) {
    if (NS_FAILED(aResult)) {
      // Service-initiated abort, no need to send transaction update
      FullscreenRequest::Reject(originDoc, aElement, aPromise);
      return;
    }

    using Result = Document::ElementReadyCheckResult;
    nsresult tickResult = NS_OK;
    const auto check = originDoc->FullscreenElementReadyCheck(
        aElement, aPromise, Some(aOptions.mKeyboardLock), aCallerType);
    if (check == Result::eOk) {
      FullscreenPaintBarrier::ArmForDocument(originDoc, true);
      originDoc->ApplyFullscreen(aElement);
    } else if (check == Result::eErrorPromiseRejected) {
      // FullscreenElementReadyCheck has already rejected aPromise.
      tickResult = NS_ERROR_ABORT;
    }
    if (NS_SUCCEEDED(tickResult)) {
      originDoc->SetFullscreenKeyboardLockStatus(aOptions.mKeyboardLock);
      aPromise->MaybeResolveWithUndefined();
    }
    (void)wgc->SendFullscreenServiceTransactionComplete(
        tickResult, MaybeDiscardedBrowsingContext{bc}, requestId);
  };

  (void)wgc->SendRequestFullscreen(
      requestId, aOptions.mKeyboardLock == FullscreenKeyboardLock::Browser,
      std::move(onResolve),
      [](auto&&) { /* actor torn down; service timer will catch */ });
}

/* static */
void FullscreenService::SendRequestExitFullscreen(Document* aDocument,
                                                  Promise* aPromise) {
  MOZ_ASSERT(aDocument);

  WindowGlobalChild* wgc = aDocument->GetWindowGlobalChild();
  if (!wgc) {
    aPromise->MaybeRejectWithTypeError("No WindowGlobalChild");
    return;
  }

  uint64_t requestId = wgc->NewFullscreenTransactionId();
  RefPtr<BrowsingContext> bc = aDocument->GetBrowsingContext();

  auto onResolve = [promise = RefPtr{aPromise}, bc = RefPtr{bc},
                    wgc = RefPtr{wgc}, doc = RefPtr{aDocument},
                    requestId](nsresult aResult) {
    FULLSCREEN_LOG("Document::ExitFullscreen: Resolve, status={}", aResult);
    if (aResult == NS_ERROR_ABORT) {
      promise->MaybeRejectWithTypeError("AbortError");
      return;
    } else if (NS_SUCCEEDED(aResult)) {
      doc->RestorePreviousFullscreenState();
    }
    promise->MaybeResolveWithUndefined();
    (void)wgc->SendFullscreenServiceTransactionComplete(
        NS_OK, MaybeDiscardedBrowsingContext{bc}, requestId);
  };

  auto onReject = [promise = RefPtr{aPromise}](auto&& err) {
    promise->MaybeRejectWithUndefined();
  };

  wgc->SendRequestExitFullscreen(requestId, std::move(onResolve),
                                 std::move(onReject));
}

/* static */
void FullscreenService::RecvRequestFullscreen(
    CanonicalBrowsingContext* aContext, uint64_t aChildRequestId,
    bool aKeyboardLock, EnterResolve&& aResolve) {
  auto* topWindow = aContext->GetTopCrossChromeBoundaryDOMWindow();
  if (!topWindow || !aContext->GetCurrentWindowGlobal()) {
    MOZ_LOG(GetLogModule(), LogLevel::Warning,
            ("Could not determine window, or window global for canonical "
             "browsing context %" PRIu64,
             aContext->Id()));
    aResolve(NS_ERROR_ABORT);
    return;
  }

  Get()
      ->Manager(topWindow->WindowID())
      ->QueueEnterRequest(aContext, aKeyboardLock, aChildRequestId,
                          std::move(aResolve));
}

/* static */
void FullscreenService::RecvRequestExitFullscreen(
    CanonicalBrowsingContext* aContext, uint64_t aChildRequestId,
    ExitResolve&& aResolve) {
  auto* topWindow = aContext->GetTopCrossChromeBoundaryDOMWindow();
  if (!topWindow) {
    aResolve(NS_ERROR_ABORT);
    return;
  }

  Get()
      ->Manager(topWindow->WindowID())
      ->QueueExitRequest(aContext, aChildRequestId, std::move(aResolve));
}

/* static */
void FullscreenService::ReceivedFullscreenTransaction(
    const MaybeDiscardedBrowsingContext& aContext, uint64_t aChildRequestId,
    nsresult aResult) {
  if (aContext.IsNullOrDiscarded()) {
    return;
  }

  CanonicalBrowsingContext* bc = aContext->Canonical();
  auto* topWindow = bc ? bc->GetTopCrossChromeBoundaryDOMWindow() : nullptr;
  if (!topWindow) {
    return;
  }

  FullscreenManager* manager = Get()->GetManager(topWindow->WindowID());
  if (!manager) {
    return;
  }
  manager->OnTransactionResponse(bc, aChildRequestId, aResult);
}

/* static */
void FullscreenService::FullscreenChanged(uint64_t aWindowId,
                                          Document* aChromeDoc,
                                          bool aIsEnteringFullscreen) {
  FULLSCREEN_LOG(
      "FullscreenService::FullscreenChanged window={}, entered fullscreen={}",
      aWindowId, aIsEnteringFullscreen);
  FullscreenManager* manager = Get()->GetManager(aWindowId);
  if (!manager) {
    return;
  }

  manager->OnFullscreenChanged(aChromeDoc, aIsEnteringFullscreen);
}

/*static*/
MOZ_CAN_RUN_SCRIPT_BOUNDARY
void FullscreenService::DispatchEnteredFullscreenChromeEvent(
    Document* aDoc, CanonicalBrowsingContext* aContext) {
  MOZ_ASSERT(aContext);
  const auto* window = aContext->GetTopCrossChromeBoundaryDOMWindow();
  if (!window || !aDoc) {
    return;
  }

  class Runnable final : public mozilla::Runnable {
   public:
    Runnable(Document* aDoc, CanonicalBrowsingContext* aContextId)
        : mozilla::Runnable("DispatchEnteredFullscreenChromeEvent"),
          mDoc(aDoc),
          mContext(aContextId) {
      MOZ_ASSERT(mContext);
    }

    MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHOD Run() final {
      RefPtr<nsGlobalWindowOuter> window =
          mContext->GetTopCrossChromeBoundaryDOMWindow();
      if (!window || !mDoc) {
        return NS_OK;
      }
      const RefPtr<CustomEvent> event =
          NS_NewDOMCustomEvent(MOZ_KnownLive(mDoc), nullptr, nullptr);
      MOZ_DIAGNOSTIC_ASSERT(event);
      AutoJSAPI jsapi;
      MOZ_DIAGNOSTIC_ASSERT(jsapi.Init(event->GetParentObject()));

      nsCString originCStr;

      mContext->GetCurrentWindowGlobal()
          ->DocumentPrincipal()
          ->GetOriginNoSuffix(originCStr);
      nsString originNoSuffix = NS_ConvertUTF8toUTF16(originCStr);

      JSContext* cx = jsapi.cx();
      JSString* jsstring = JS_NewUCStringCopyN(cx, originNoSuffix.get(),
                                               originNoSuffix.Length());

      JS::Rooted<JS::Value> urlStringVal(cx, JS::StringValue(jsstring));

      event->InitCustomEvent(cx, FullscreenService::sEnteredFullscreenEvent,
                             true, false, urlStringVal);
      event->SetTrusted(true);

      AsyncEventDispatcher::RunDOMEventWhenSafe(*window, *event,
                                                ChromeOnlyDispatch::eYes);

      return NS_OK;
    }

   private:
    RefPtr<Document> mDoc;
    RefPtr<CanonicalBrowsingContext> mContext;
  };

  nsCOMPtr<nsIRunnable> runnable = new Runnable(aDoc, aContext);
  NS_DispatchToMainThread(runnable);
}

/** static */
void FullscreenService::DispatchExitedFullscreenChromeEvent(
    nsGlobalWindowOuter* aWindow) {
  if (aWindow) {
    nsContentUtils::DispatchEventOnlyToChrome(
        aWindow->GetExtantDoc(), aWindow,
        FullscreenService::sExitedFullscreenEvent, CanBubble::eYes,
        Cancelable::eNo);
  }
}

FullscreenManager* FullscreenService::Manager(uint64_t aWindowId) {
  for (auto& manager : mManagers) {
    if (manager->Id() == aWindowId) {
      return manager.get();
    }
  }
  mManagers.AppendElement(MakeUnique<FullscreenManager>(aWindowId));
  return mManagers.LastElement().get();
}

FullscreenManager* FullscreenService::GetManager(uint64_t aWindowId) {
  for (auto& manager : mManagers) {
    if (manager->Id() == aWindowId) {
      return manager.get();
    }
  }
  return nullptr;
}

/* static */
void FullscreenService::OnWindowOuterDestroyed(uint64_t aWindowId) {
  // Don't lazy-create the singleton during teardown.
  if (sIsXPCOMShutdown || !gFullscreenService) {
    return;
  }
  auto& managers = gFullscreenService->mManagers;
  for (size_t i = 0; i < managers.Length(); ++i) {
    if (managers[i]->Id() == aWindowId) {
      FULLSCREEN_LOG("Reaping FullscreenManager for window {}", aWindowId);
      managers.RemoveElementAt(i);
      return;
    }
  }
}

NS_IMETHODIMP
FullscreenService::Observe(nsISupports* aSubject, const char* aTopic,
                           const char16_t* aData) {
  if (!strcmp(aTopic, NS_XPCOM_WILL_SHUTDOWN_OBSERVER_ID)) {
    MOZ_ASSERT_IF(FullscreenService::Enabled(), gFullscreenService);
    if (nsCOMPtr<nsIObserverService> obs =
            mozilla::services::GetObserverService()) {
      obs->RemoveObserver(this, NS_XPCOM_WILL_SHUTDOWN_OBSERVER_ID);
    }
    Shutdown();
  }
  return NS_OK;
}

/*static*/
LogModule* FullscreenService::GetLogModule() {
  static mozilla::LazyLogModule sModule("Fullscreen");
  return static_cast<LogModule*>(sModule);
}

/*static*/
FullscreenService* FullscreenService::Get() {
  MOZ_ASSERT(NS_IsMainThread());
  // We may be called in certain GC context, when we've actually be destroyed.
  // At those times, we should return nullptr, there exists no fullscreen
  // service state anyway
  if (sIsXPCOMShutdown) {
    return nullptr;
  }

  if (!gFullscreenService) {
    gFullscreenService = new FullscreenService();
  }

  return gFullscreenService;
}

/** static */
already_AddRefed<FullscreenService> FullscreenService::GetSingleton() {
  return do_AddRef(Get());
}

/* static */
bool FullscreenService::Enabled() {
  return !StaticPrefs::full_screen_api_service_manager_disabled_AtStartup();
}

NS_IMETHODIMP
FullscreenService::GetEnabled(bool* aIsEnabled) {
  *aIsEnabled = FullscreenService::Enabled();
  return NS_OK;
}

NS_IMETHODIMP
FullscreenService::Cancel(mozilla::dom::BrowsingContext* aBrowsingContext) {
  MOZ_ASSERT(XRE_IsParentProcess() && NS_IsMainThread());
  if (NS_WARN_IF(aBrowsingContext == nullptr ||
                 aBrowsingContext->IsDiscarded())) {
    return NS_OK;
  }
  nsPIDOMWindowOuter* window =
      aBrowsingContext->Canonical()->GetTopCrossChromeBoundaryDOMWindow();
  FullscreenManager* manager = Manager(window->WindowID());
  FULLSCREEN_LOG("FullscreenService::Cancel for bc={}", aBrowsingContext->Id());
  manager->CancelFullscreen();
  return NS_OK;
}

/* static */
void FullscreenService::CancelFullscreen(
    mozilla::dom::BrowsingContext* aContext) {
  MOZ_DIAGNOSTIC_ASSERT(aContext);
  if (XRE_IsContentProcess()) {
    Document* doc = aContext->GetDocument();
    WindowGlobalChild* wgc = doc ? doc->GetWindowGlobalChild() : nullptr;
    if (wgc) {
      (void)wgc->SendCancelFullscreen();
    }
  } else {
    MOZ_ASSERT(XRE_IsParentProcess());
    Get()->Cancel(aContext);
  }
}

}  // namespace mozilla::dom
