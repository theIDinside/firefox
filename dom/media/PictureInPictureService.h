/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_PictureInPictureService_h
#define mozilla_dom_PictureInPictureService_h

#include "mozilla/dom/JSWindowActorChild.h"
#include "mozilla/dom/PictureInPictureEvent.h"
#include "mozilla/dom/PictureInPictureEventBinding.h"
#include "mozilla/dom/PromiseNativeHandler.h"
#include "nsIPictureInPictureFunctions.h"

namespace mozilla::dom {

class PictureInPictureRequest;
class PictureInPictureWindow;

/**
 * Service that communicates with platform specific implementation of PIP
 * via nsIPictureInPictureFunctions.idl.
 * Also maintains the parallel queue for the picture in picture requests.
 */
class PictureInPictureService {
 public:
  NS_INLINE_DECL_REFCOUNTING(PictureInPictureService)

  static void EnsureInit();
  static bool IsSupported();
  static void FinishRequestAndResumeNextQueued();

  /**
   * Queues the parallel steps of the enter/exit algorithm and returns the
   * promise that is to be handed to the web contents script.
   */
  static void RunInParallel(RefPtr<PictureInPictureRequest> aRequest);

  static RefPtr<Promise> RequestPictureInPictureWindow(
      HTMLVideoElement* aElement, PictureInPictureWindow* aWindow);
  static RefPtr<Promise> ExitPictureInPictureWindow(HTMLVideoElement* aElement);

  static RefPtr<Promise> CreatePromise(nsPIDOMWindowInner* aWindow,
                                       ErrorResult& aRv) {
    EnsureInit();
    if (NS_WARN_IF(!aWindow || !aWindow->GetWindowContext())) {
      aRv.ThrowInvalidStateError("No window");
      return nullptr;
    }

    RefPtr<Promise> promise = Promise::Create(aWindow->AsGlobal(), aRv);
    if (NS_WARN_IF(aRv.Failed())) {
      return nullptr;
    }

    return promise;
  }

  template <typename Request, typename... Args>
  static RefPtr<Request> CreateRequest(Promise* aPromise, Args&&... args) {
    return MakeRefPtr<Request>(aPromise, std::forward<Args>(args)...);
  }

 private:
  ~PictureInPictureService() = default;

  bool InitializeFunctions();

  nsCOMPtr<nsIPictureInPictureFunctions> mPictureInPictureFunctions;
  nsTArray<RefPtr<PictureInPictureRequest>> mWebContentRequests;
};

// Each call to videoEl.requestPictureInPicture() creates one of these, mapping
// to the steps both in and out of the parallel queue.
class PictureInPictureRequest : public PromiseNativeHandler {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(PictureInPictureRequest)

  PictureInPictureRequest(Promise* aPromise, HTMLVideoElement* aVideo);

  // In-parallel steps of the requestPictureInPicture algorithm, but running on
  // the main thread Returns true if the parallel steps has more to do
  virtual bool StartParallelSteps() = 0;

  // PromiseNativeHandler implementation
  void ResolvedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override;
  void RejectedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override;

 protected:
  virtual void OnServicePromiseSettled(bool aWasResolved) = 0;
  ~PictureInPictureRequest();

  RefPtr<PictureInPictureWindow> mPictureInPictureWindowInstance;
  RefPtr<HTMLVideoElement> mVideo;
  RefPtr<Promise> mPromise;
};

class EnterPictureInPictureRequest final : public PictureInPictureRequest {
  void FireEnterPictureInPictureEvent();

 public:
  EnterPictureInPictureRequest(Promise* aPromise, HTMLVideoElement* aVideo);
  virtual ~EnterPictureInPictureRequest() override;
  bool StartParallelSteps() override;
  void OnServicePromiseSettled(bool aResolved) override;
};

class ExitPictureInPictureRequest final : public PictureInPictureRequest {
  static void FireLeavePictureInPictureEvent(PictureInPictureWindow* aWindow);

 public:
  ExitPictureInPictureRequest(Promise* aPromise, HTMLVideoElement* aVideo);
  virtual ~ExitPictureInPictureRequest() override;
  bool StartParallelSteps() override;
  void OnServicePromiseSettled(bool aResolved) override;

 private:
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_PictureInPictureService_h
