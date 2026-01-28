/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_PictureInPictureWindow_h
#define mozilla_dom_PictureInPictureWindow_h

#include "mozilla/Attributes.h"
#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/dom/EventHandlerBinding.h"

namespace mozilla {
class ErrorResult;

namespace dom {
class HTMLVideoElement;

class PictureInPictureWindow final : public DOMEventTargetHelper {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(PictureInPictureWindow,
                                           DOMEventTargetHelper)

  static bool PictureInPictureEnabled();
  explicit PictureInPictureWindow(nsPIDOMWindowInner* aWindow,
                                  HTMLVideoElement* aVideoElement);

  // WebIDL interface
  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  // PictureInPictureWindow WebIDL
  int32_t Width() const;
  int32_t Height() const;

  void NotifyDimensionsChanged(int32_t aWidth, int32_t aHeight);

  IMPL_EVENT_HANDLER(resize)

 protected:
  virtual ~PictureInPictureWindow();

 private:
  bool IsStateOpened() const { return mVideoElement; }


  int32_t mWidth;
  int32_t mHeight;
  RefPtr<HTMLVideoElement> mVideoElement;
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_PictureInPictureWindow_h