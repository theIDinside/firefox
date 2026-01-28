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
 * Service that communicates with PictureInPicture.sys.mjs and its launcher. 
 * Also maintains the parallel queue for the picture in picture request.
 * (see parallel queues: https://html.spec.whatwg.org/#parallel-queue)
 */
class PictureInPictureService {
 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(PictureInPictureService)
  NS_DECL_CYCLE_COLLECTION_NATIVE_CLASS(PictureInPictureService)

  static void Init();
  static void MaybeRunParallelSteps();
  static void QueueParallelSteps(RefPtr<PictureInPictureRequest> aRequest);

  static already_AddRefed<Promise> CreateAndMaybeQueueParallelSteps(
      HTMLVideoElement* aVideo, ErrorResult& aRv);

  static nsIPictureInPictureFunctions* GetFunctions();
  static RefPtr<Promise> RequestPictureInPictureWindow(HTMLVideoElement* aElement, PictureInPictureWindow* aWindow);

 private:
  ~PictureInPictureService() = default;

  nsCOMPtr<nsIPictureInPictureFunctions> mPictureInPictureFunctions;
  uint64_t sNextRequestId = 0;
  nsTArray<RefPtr<PictureInPictureRequest>> mWebContentRequests;
};

// Each call to videoEl.requestPictureInPicture() creates one of these, mapping
// to the steps both in and out of the parallel queue.
class PictureInPictureRequest final : public PromiseNativeHandler {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(PictureInPictureRequest)

  PictureInPictureRequest(HTMLVideoElement* aVideo, RefPtr<Promise> aPromise, uint64_t aId);

  static RefPtr<PictureInPictureRequest> CreateParallelStepsRequest(
      HTMLVideoElement* aVideo, uint64_t aRequestId, ErrorResult& aRv);

  // In-parallel steps of the requestPictureInPicture algorithm, but running on
  // the main thread Returns true if the parallel steps has more to do
  bool ExecuteParallelSteps();
  void QueueGlobalTaskSteps(bool aSuccess);

  RefPtr<Promise> WebContentPromise();
  uint64_t Id() const { return mRequestId; }

  // PromiseNativeHandler implementation
  void ResolvedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override;
  void RejectedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override;

 private:
  ~PictureInPictureRequest();
  bool RequestPictureInPicture(nsPIDOMWindowInner* aWindow);

  uint64_t mRequestId;
  RefPtr<PictureInPictureWindow> mPictureInPictureWindowInstance;
  RefPtr<HTMLVideoElement> mVideo;
  RefPtr<Promise> mPromise;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_PictureInPictureService_h