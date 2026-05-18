/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FullscreenService.h"

#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_full_screen_api.h"
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
