/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PictureInPictureService.h"

#include "PictureInPictureWindow.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/HTMLVideoElement.h"
#include "mozilla/dom/PictureInPictureWindow.h"
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

void PictureInPictureService::EnsureInit() {
  if (!gPictureInPictureService) [[unlikely]] {
    gPictureInPictureService = new PictureInPictureService();
    if (!gPictureInPictureService->InitializeFunctions()) {
      NS_WARNING(
          "Video Picture-in-Picture not yet supported on this platform.");
    }
  }
}

// Platforms that do not implement PictureInPictureFunctions have video
// PictureInPicture disabled.
bool PictureInPictureService::IsSupported() {
  EnsureInit();
  return gPictureInPictureService &&
         gPictureInPictureService->mPictureInPictureFunctions;
}

void PictureInPictureService::FinishRequestAndResumeNextQueued() {
  MOZ_ASSERT(NS_IsMainThread());

  gPictureInPictureService->mWebContentRequests.RemoveElementAt(0);

  if (!gPictureInPictureService->mWebContentRequests.IsEmpty()) {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "PictureInPictureService::QueueNewRequestForParallelSteps", []() {
          if (!gPictureInPictureService->mWebContentRequests.IsEmpty()) {
            if (gPictureInPictureService->mWebContentRequests[0]
                    ->StartParallelSteps()) {
            }
          }
        }));
  }
}

/* static */
void PictureInPictureService::RunInParallel(
    RefPtr<PictureInPictureRequest> aRequest) {
  MOZ_ASSERT(NS_IsMainThread());

  gPictureInPictureService->mWebContentRequests.AppendElement(aRequest);

  if (gPictureInPictureService->mWebContentRequests.Length() == 1) {
    gPictureInPictureService->mWebContentRequests.LastElement()
        ->StartParallelSteps();
  }
}

/* static */
RefPtr<Promise> PictureInPictureService::RequestPictureInPictureWindow(
    HTMLVideoElement* aElement, PictureInPictureWindow* aWindow) {
  if (!IsSupported()) {
    return nullptr;
  }

  AutoJSAPI jsapi;
  nsPIDOMWindowInner* window = aElement->OwnerDoc()->GetInnerWindow();
  if (NS_WARN_IF(!jsapi.Init(window))) {
    return nullptr;
  }
  JSContext* cx = jsapi.cx();

  RefPtr<Promise> chromePromise;
  nsresult rv = gPictureInPictureService->mPictureInPictureFunctions
                    ->OpenPictureInPictureWindow(aElement, aWindow, cx,
                                                 getter_AddRefs(chromePromise));
  if (NS_WARN_IF(NS_FAILED(rv) || !chromePromise)) {
    return nullptr;
  }

  return chromePromise;
}

/* static */
RefPtr<Promise> PictureInPictureService::ExitPictureInPictureWindow(
    HTMLVideoElement* aElement) {
  if (!IsSupported()) {
    return nullptr;
  }

  AutoJSAPI jsapi;
  nsPIDOMWindowInner* window = aElement->OwnerDoc()->GetInnerWindow();
  if (NS_WARN_IF(!jsapi.Init(window))) {
    return nullptr;
  }
  JSContext* cx = jsapi.cx();

  RefPtr<Promise> chromePromise;
  nsresult rv = gPictureInPictureService->mPictureInPictureFunctions
                    ->ClosePictureInPictureWindow(
                        aElement, cx, getter_AddRefs(chromePromise));
  if (NS_WARN_IF(NS_FAILED(rv) || !chromePromise)) {
    return nullptr;
  }

  return chromePromise;
}

bool PictureInPictureService::InitializeFunctions() {
  if (!mPictureInPictureFunctions) {
    mPictureInPictureFunctions =
        do_CreateInstance("@mozilla.org/toolkit/pictureinpicture-functions;1");
    if (NS_WARN_IF(!mPictureInPictureFunctions)) {
      return false;
    }
  }
  return true;
}

PictureInPictureRequest::PictureInPictureRequest(Promise* aPromise,
                                                 HTMLVideoElement* aVideo)
    : mVideo(aVideo), mPromise(aPromise) {
  MOZ_ASSERT(aPromise);
}

void PictureInPictureRequest::ResolvedCallback(JSContext* aCx,
                                               JS::Handle<JS::Value> aValue,
                                               ErrorResult& aRv) {
  MOZ_ASSERT(NS_IsMainThread());
  OnServicePromiseSettled(true);
  PictureInPictureService::FinishRequestAndResumeNextQueued();
}

void PictureInPictureRequest::RejectedCallback(JSContext* aCx,
                                               JS::Handle<JS::Value> aValue,
                                               ErrorResult& aRv) {
  MOZ_ASSERT(NS_IsMainThread());
  OnServicePromiseSettled(false);
  PictureInPictureService::FinishRequestAndResumeNextQueued();
}

EnterPictureInPictureRequest::EnterPictureInPictureRequest(
    Promise* aPromise, HTMLVideoElement* aVideo)
    : PictureInPictureRequest(std::move(aPromise), aVideo) {}

EnterPictureInPictureRequest::~EnterPictureInPictureRequest() {}

bool EnterPictureInPictureRequest::StartParallelSteps() {
  Document* doc = mVideo->OwnerDoc();
  nsPIDOMWindowInner* window = doc ? doc->GetInnerWindow() : nullptr;
  if (!doc || !window) {
    mPromise->MaybeRejectWithInvalidStateError("No document or window");
    return false;
  }

  // 9.1 If this is pictureInPictureElement:
  if (doc->GetPictureInPictureElementInternal() == mVideo) {
    // 9.1.1 Queue a global task on the media element event task source given
    // global to resolve p with the Picture-in-Picture window associated with
    // pictureInPictureElement
    // 9.1.2 abort these steps
    MOZ_ASSERT(mVideo->GetAssociatedPictureInPictureWindow());
    mPromise->MaybeResolve(mVideo->GetAssociatedPictureInPictureWindow());
    PictureInPictureService::FinishRequestAndResumeNextQueued();
    return false;
  }

  // Stash the PIP Window instance for later use as well as we need to be
  // able to provide it to the picture in picture functions (see
  // nsIPictureInPictureFunctions.idl)
  mPictureInPictureWindowInstance =
      MakeRefPtr<PictureInPictureWindow>(window, mVideo);

  // 9.2 Let Picture-in-Picture window be a new instance of
  // PictureInPictureWindow associated with this.
  RefPtr<Promise> servicePromise =
      PictureInPictureService::RequestPictureInPictureWindow(
          mVideo, mPictureInPictureWindowInstance);
  if (!servicePromise) {
    // Spec Note: A user agent may abort when it deems necessary, e.g. due to an
    // error. When doing so it must queue a global task on the media element
    // event task source given global and reject the promise with an
    // InvalidStateError.
    mPromise->MaybeRejectWithInvalidStateError("Failed to create PIP Window");
    return false;
  }

  // Remainder of parallel steps happen when 9.2 completes
  servicePromise->AppendNativeHandler(this);
  return true;
}

void EnterPictureInPictureRequest::FireEnterPictureInPictureEvent() {
  // 9.3.4 Fire an event named enterpictureinpicture using PictureInPictureEvent
  // at the video with its bubbles attribute initialized to true and its
  // pictureInPictureWindow attribute initialized to Picture-in-Picture window.
  PictureInPictureEventInit eventInit;
  eventInit.mBubbles = true;
  eventInit.mCancelable = false;
  eventInit.mPictureInPictureWindow = mPictureInPictureWindowInstance;

  RefPtr<PictureInPictureEvent> pipEvent = PictureInPictureEvent::Constructor(
      mVideo, u"enterpictureinpicture"_ns, eventInit);
  pipEvent->SetTrusted(true);
  mVideo->EventTarget::DispatchEvent(*pipEvent);
}

void EnterPictureInPictureRequest::OnServicePromiseSettled(bool aResolved) {
  // 9.3 Queue a global task on the media element event task source given
  // global, to perform the following steps:
  if (aResolved && mVideo && mPromise) {
    // 9.3.1 Set pictureInPictureElement to this.
    if (Document* doc = mVideo->OwnerDoc()) {
      doc->SetPictureInPictureElement(mVideo);
    }
    // TODO: *Maybe*? This is a hack in the spec. Other specs
    // 9.3.2 Append relevant settings object’s origin to initiators of active
    // Picture-in-Picture sessions.

    mVideo->AddStates(ElementState::PICTURE_IN_PICTURE);
    mVideo->SetAssociatedPictureInPictureWindow(
        mPictureInPictureWindowInstance);

    // 9.3.3 If pictureInPictureElement is fullscreenElement, it is RECOMMENDED
    // to exit fullscreen.

    // 9.3.4
    FireEnterPictureInPictureEvent();

    // 9.3.5 Resolve p with the Picture-in-Picture window associated with
    // pictureInPictureElement.
    mPromise->MaybeResolve(mPictureInPictureWindowInstance);
  } else if (!aResolved && mPromise) {
    mPromise->MaybeRejectWithInvalidStateError(
        "Picture-in-Picture request failed");
  }
}

ExitPictureInPictureRequest::ExitPictureInPictureRequest(
    Promise* aPromise, HTMLVideoElement* aVideo)
    : PictureInPictureRequest(std::move(aPromise), aVideo) {}

ExitPictureInPictureRequest::~ExitPictureInPictureRequest() {}

/* static */
void ExitPictureInPictureRequest::FireLeavePictureInPictureEvent(
    PictureInPictureWindow* aWindow) {
  aWindow->Close();
}

// https://w3c.github.io/picture-in-picture/#exit-pip
bool ExitPictureInPictureRequest::StartParallelSteps() {
  Document* doc = mVideo->OwnerDoc();

  // Note: Spec needs update for this check, due to it's in-parallel nature.
  if (!doc->GetPictureInPictureElementInternal()) {
    mPromise->MaybeResolveWithUndefined();
    PictureInPictureService::FinishRequestAndResumeNextQueued();
    return false;
  }

  // 2. Run the close window algorithm with the Picture-in-Picture window
  // associated with pictureInPictureElement.
  RefPtr<Promise> servicePromise =
      PictureInPictureService::ExitPictureInPictureWindow(mVideo);
  // Note: PR for requestPictureInPicture has language like "if user agent deems
  // necessary, it can abort by rejecting with invalid state error".
  // Same should apply here.
  if (!servicePromise) {
    mPromise->MaybeRejectWithInvalidStateError(
        "Failed to create exit picture in picture request.");
    return false;
  }

  // Remainder of exit steps happen in HTMLVideoElement::EndCloningVisually
  // as this can be called via web content JS, but also via the "native to
  // Firefox" PIP implementation.
  servicePromise->AppendNativeHandler(this);
  return true;
}

void ExitPictureInPictureRequest::OnServicePromiseSettled(bool aResolved) {
  if (!aResolved) {
    mPromise->MaybeRejectWithInvalidStateError("PiP request failed");
    return;
  }

  mPromise->MaybeResolveWithUndefined();
}

}  // namespace mozilla::dom
