/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FullscreenService.h"

#include "CustomEvent.h"
#include "FullscreenManager.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_full_screen_api.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/Document.h"
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

}  // namespace mozilla::dom
