/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_FullscreenService_h
#define mozilla_dom_FullscreenService_h

#include "mozilla/Logging.h"
#include "nsIFullscreenService.h"
#include "nsIObserver.h"

namespace mozilla::dom {

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

  // called during XPCOM will shut down phase
  void Shutdown();

 private:
  FullscreenService();
  // A private destructor must be declared.
  ~FullscreenService() = default;
};

}  // namespace mozilla::dom

#define FULLSCREEN_LOG(FMT_STRING, ...)                                \
  MOZ_LOG_FMT(dom::FullscreenService::GetLogModule(), LogLevel::Debug, \
              FMT_STRING __VA_OPT__(, ) __VA_ARGS__)

#endif  // mozilla_FullscreenService_h
