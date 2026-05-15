/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_FullscreenPaintBarrier_h
#define mozilla_dom_FullscreenPaintBarrier_h

#include <cstdint>

#include "nsCOMPtr.h"
#include "nsIDOMEventListener.h"

class nsPIDOMWindowInner;

namespace mozilla::dom {

class Document;

// One-shot helper that observes the next compositor paint following a
// fullscreen state mutation in a given document, and notifies the parent
// process so that the "fullscreen-painted" observer notification can be
// fired on the chrome window.
//
// `ArmForDocument` snapshots `lastTransactionId` for the document's window
// and attaches a `MozAfterPaint` listener on the inner window. On the first
// paint whose `transactionId` exceeds the snapshot the listener detaches
// itself and sends `BrowserChild::SendFullscreenPainted()`.
class FullscreenPaintBarrier final : public nsIDOMEventListener {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIDOMEVENTLISTENER

  // No-op when the document has no inner window, no docshell, or no
  // BrowserChild (parent-process chrome documents such as the PiP player
  // produce `fullscreen-painted` themselves).
  //
  // `aIsEnter` is sent to the parent via IPC. The corresponding
  // "fullscreen-painted" notification should only be sent when the top-most
  // browsing context is done for exit, whereas for enter, it's the leaf
  // context.
  static void ArmForDocument(Document* aDoc, bool aIsEnter);

 private:
  FullscreenPaintBarrier(nsPIDOMWindowInner* aWindow, uint64_t aSnapshot,
                         bool aIsEnter);
  ~FullscreenPaintBarrier();

  void Detach();

  nsCOMPtr<nsPIDOMWindowInner> mWindow;
  uint64_t mSnapshot;
  bool mIsEnter;
  bool mAttached = false;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_FullscreenPaintBarrier_h
