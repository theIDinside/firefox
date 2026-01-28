/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PictureInPictureService.h"

#include "PictureInPictureWindow.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/HTMLVideoElement.h"
#include "mozilla/dom/PictureInPictureWindow.h"
#include "mozilla/dom/WindowContext.h"
#include "nsComponentManagerUtils.h"

namespace mozilla::dom {

StaticRefPtr<PictureInPictureService> gPictureInPictureService;
PictureInPictureRequest::~PictureInPictureRequest() = default;

NS_IMPL_CYCLE_COLLECTION(PictureInPictureRequest, mVideo, mPromise,
                         mPictureInPictureWindowInstance)
NS_IMPL_CYCLE_COLLECTING_ADDREF(PictureInPictureRequest)
NS_IMPL_CYCLE_COLLECTING_RELEASE(PictureInPictureRequest)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(PictureInPictureRequest)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION(PictureInPictureService, mWebContentRequests)

void PictureInPictureService::Init() {
  if (!gPictureInPictureService) {
    gPictureInPictureService = new PictureInPictureService();
  }
}

void PictureInPictureService::MaybeRunParallelSteps() {
  MOZ_ASSERT(NS_IsMainThread());

  gPictureInPictureService->mWebContentRequests.RemoveElementAt(0);

  if (!gPictureInPictureService->mWebContentRequests.IsEmpty()) {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "PictureInPictureService::QueueNewRequestForParallelSteps", []() {
          if (!gPictureInPictureService->mWebContentRequests.IsEmpty()) {
            if (gPictureInPictureService->mWebContentRequests[0]
                    ->ExecuteParallelSteps()) {
            }
          }
        }));
  }
}

/* static */
void PictureInPictureService::QueueParallelSteps(
    RefPtr<PictureInPictureRequest> aRequest) {
  MOZ_ASSERT(NS_IsMainThread());

  gPictureInPictureService->mWebContentRequests.AppendElement(
      std::move(aRequest));

  if (gPictureInPictureService->mWebContentRequests.Length() == 1) {
    gPictureInPictureService->mWebContentRequests.LastElement()
        ->ExecuteParallelSteps();
  }
}

/* static */
RefPtr<Promise> PictureInPictureService::RequestPictureInPictureWindow(
    HTMLVideoElement* aElement, PictureInPictureWindow* aWindow) {

  if (!gPictureInPictureService->mPictureInPictureFunctions) {
    gPictureInPictureService->mPictureInPictureFunctions =
        do_CreateInstance("@mozilla.org/toolkit/pictureinpicture-functions;1");
    if (NS_WARN_IF(!gPictureInPictureService->mPictureInPictureFunctions)) {
      return nullptr;
    }
  }

  AutoJSAPI jsapi;
  nsPIDOMWindowInner* window = aElement->OwnerDoc()->GetInnerWindow();
  if (NS_WARN_IF(!jsapi.Init(window))) {
    return nullptr;
  }
  JSContext* cx = jsapi.cx();

  JS::Rooted<JS::Value> pipWindowVal(cx);
  if (NS_WARN_IF(!ToJSValue(cx, aWindow, &pipWindowVal))) {
    return nullptr;
  }

  RefPtr<Promise> chromePromise;
  nsresult rv =
      gPictureInPictureService->mPictureInPictureFunctions
          ->RequestPictureInPictureWindow(aElement, pipWindowVal, cx,
                                          getter_AddRefs(chromePromise));
  if (NS_WARN_IF(NS_FAILED(rv) || !chromePromise)) {
    return nullptr;
  }

  return chromePromise;
}

/* static */
already_AddRefed<Promise>
PictureInPictureService::CreateAndMaybeQueueParallelSteps(
    HTMLVideoElement* aVideo, ErrorResult& aRv) {
  MOZ_ASSERT(aVideo);
  Document* doc = aVideo->OwnerDoc();
  nsPIDOMWindowInner* window = doc->GetInnerWindow();
  if (NS_WARN_IF(!window)) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return nullptr;
  }

  RefPtr<PictureInPictureRequest> request =
      PictureInPictureRequest::CreateParallelStepsRequest(
          aVideo, ++gPictureInPictureService->sNextRequestId, aRv);
  if (!request) {
    return nullptr;
  }

  RefPtr<Promise> p = request->WebContentPromise();

  if (!PictureInPictureWindow::PictureInPictureEnabled()) {
    p->MaybeRejectWithNotSupportedError("Picture-In-Picture is not enabled");
    return p.forget();
  }

  if (aVideo->ReadyState() == HTMLMediaElement_Binding::HAVE_NOTHING) {
    p->MaybeRejectWithInvalidStateError("Video readyState is HAVE_NOTHING");
    return p.forget();
  }

  if (aVideo->DisablePictureInPicture()) {
    p->MaybeRejectWithInvalidStateError(
        "Picture-in-Picture is disabled on this video");
    return p.forget();
  }

  if (!window->GetWindowContext() ||
      !window->GetWindowContext()->HasValidTransientUserGestureActivation()) {
    p->MaybeRejectWithNotAllowedError(
        "Picture-in-Picture requires user activation");
    return p.forget();
  }

  QueueParallelSteps(std::move(request));

  return p.forget();
}

PictureInPictureRequest::PictureInPictureRequest(HTMLVideoElement* aVideo,
                                                 RefPtr<Promise> aPromise,
                                                 uint64_t aId)
    : mRequestId(aId), mVideo(aVideo), mPromise(std::move(aPromise)) {}

/* static */
RefPtr<PictureInPictureRequest>
PictureInPictureRequest::CreateParallelStepsRequest(HTMLVideoElement* aVideo,
                                                    uint64_t aRequestId,
                                                    ErrorResult& aRv) {
  Document* doc = aVideo->OwnerDoc();
  nsPIDOMWindowInner* window = doc->GetInnerWindow();
  if (NS_WARN_IF(!window)) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return nullptr;
  }

  RefPtr<Promise> promise = Promise::Create(window->AsGlobal(), aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  return MakeRefPtr<PictureInPictureRequest>(aVideo, std::move(promise),
                                             aRequestId);
}

bool PictureInPictureRequest::ExecuteParallelSteps() {
  Document* doc = mVideo->OwnerDoc();
  if (!doc) {
    mPromise->MaybeRejectWithInvalidStateError("No doc");
    return false;
  }

  // If already in PiP, just resolve
  if (doc->GetPictureInPictureElementInternal() == mVideo) {
    MOZ_ASSERT(mVideo->GetAssociatedPictureInPictureWindow());
    mPromise->MaybeResolve(mVideo->GetAssociatedPictureInPictureWindow());
    PictureInPictureService::MaybeRunParallelSteps();
    return true;
  }

  nsPIDOMWindowInner* window = doc->GetInnerWindow();
  if (!window) {
    mPromise->MaybeRejectWithInvalidStateError("No window");
    return false;
  }

  mPictureInPictureWindowInstance =
      MakeRefPtr<PictureInPictureWindow>(window, mVideo);

  return RequestPictureInPicture(window);
}

void PictureInPictureRequest::QueueGlobalTaskSteps(bool aSuccess) {
  if (aSuccess && mVideo && mPromise) {
    // Set this element as the current picture-in-picture element
    if (Document* doc = mVideo->OwnerDoc()) {
      doc->SetPictureInPictureElement(mVideo);
    }

    mVideo->SetAssociatedPictureInPictureWindow(
        mPictureInPictureWindowInstance);

    // Fire enterpictureinpicture event
    PictureInPictureEventInit eventInit;
    eventInit.mBubbles = true;
    eventInit.mCancelable = false;
    eventInit.mPictureInPictureWindow = mPictureInPictureWindowInstance;

    RefPtr<PictureInPictureEvent> pipEvent = PictureInPictureEvent::Constructor(
        mVideo, u"enterpictureinpicture"_ns, eventInit);
    pipEvent->SetTrusted(true);
    MakeRefPtr<AsyncEventDispatcher>(mPictureInPictureWindowInstance,
                                     pipEvent.forget())
        ->PostDOMEvent();
    mPromise->MaybeResolve(mPictureInPictureWindowInstance);
  } else if (mPromise) {
    mPromise->MaybeRejectWithInvalidStateError(
        "Picture-in-Picture request failed");
  }

  PictureInPictureService::MaybeRunParallelSteps();
}

void PictureInPictureRequest::ResolvedCallback(JSContext* aCx,
                                               JS::Handle<JS::Value> aValue,
                                               ErrorResult& aRv) {
  MOZ_ASSERT(NS_IsMainThread());
  QueueGlobalTaskSteps(true);
}

void PictureInPictureRequest::RejectedCallback(JSContext* aCx,
                                               JS::Handle<JS::Value> aValue,
                                               ErrorResult& aRv) {
  MOZ_ASSERT(NS_IsMainThread());
  QueueGlobalTaskSteps(false);
}

bool PictureInPictureRequest::RequestPictureInPicture(
    nsPIDOMWindowInner* aWindow) {
  RefPtr<Promise> promise =
      PictureInPictureService::RequestPictureInPictureWindow(
          mVideo, mPictureInPictureWindowInstance);
  if (!promise) {
    mPromise->MaybeRejectWithInvalidStateError("Failed to create PIP Window");
    return false;
  }
  promise->AppendNativeHandler(this);
  return true;
}

RefPtr<Promise> PictureInPictureRequest::WebContentPromise() {
  return mPromise;
}

}  // namespace mozilla::dom
