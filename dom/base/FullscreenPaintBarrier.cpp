/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FullscreenPaintBarrier.h"

#include "mozilla/RefPtr.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/EventTarget.h"
#include "mozilla/dom/NotifyPaintEvent.h"
#include "nsGlobalWindowInner.h"
#include "nsIDocShell.h"
#include "nsIDocShellTreeItem.h"
#include "nsPIDOMWindow.h"
#include "nsPresContext.h"
#include "nsRefreshDriver.h"

namespace mozilla::dom {

NS_IMPL_ISUPPORTS(FullscreenPaintBarrier, nsIDOMEventListener)

FullscreenPaintBarrier::FullscreenPaintBarrier(nsPIDOMWindowInner* aWindow,
                                               uint64_t aSnapshot,
                                               bool aIsEnter)
    : mWindow(aWindow), mSnapshot(aSnapshot), mIsEnter(aIsEnter) {}

FullscreenPaintBarrier::~FullscreenPaintBarrier() = default;

/* static */
void FullscreenPaintBarrier::ArmForDocument(Document* aDoc, bool aIsEnter) {
  if (!aDoc) {
    return;
  }
  nsCOMPtr<nsPIDOMWindowInner> win = aDoc->GetInnerWindow();
  if (!win) {
    return;
  }
  nsIDocShell* ds = aDoc->GetDocShell();
  if (!ds) {
    return;
  }

  const bool notParentProcessAndNoBrowserChild =
      !XRE_IsParentProcess() && !BrowserChild::GetFrom(ds);
  if (notParentProcessAndNoBrowserChild) {
    return;
  }

  nsCOMPtr<nsIDocShellTreeItem> rootTreeItem;
  ds->GetInProcessRootTreeItem(getter_AddRefs(rootTreeItem));
  nsCOMPtr<nsIDocShell> rootDocShell = do_QueryInterface(rootTreeItem);
  if (!rootDocShell) {
    return;
  }
  nsPresContext* pc = rootDocShell->GetPresContext();
  if (!pc) {
    return;
  }
  uint64_t snapshot = uint64_t(pc->RefreshDriver()->LastTransactionId());

  RefPtr<EventTarget> target = nsGlobalWindowInner::Cast(win);
  if (!target) {
    return;
  }

  RefPtr<FullscreenPaintBarrier> barrier =
      new FullscreenPaintBarrier(win, snapshot, aIsEnter);
  if (NS_FAILED(target->AddEventListener(u"MozAfterPaint"_ns, barrier,
                                         /* aUseCapture = */ false,
                                         /* aWantsUntrusted = */ false))) {
    return;
  }
  // The listener registry holds a strong ref on `barrier` for as long as
  // it remains attached, keeping the object alive until Detach() runs.
  barrier->mAttached = true;
  FULLSCREEN_LOG("Armed paint notification for transition={} for doc={:p}",
                 aIsEnter ? "enter" : "exit", (void*)aDoc);
}

NS_IMETHODIMP
FullscreenPaintBarrier::HandleEvent(Event* aEvent) {
  if (!aEvent) {
    return NS_OK;
  }
  WidgetEvent* widgetEvent = aEvent->WidgetEventPtr();
  if (!widgetEvent || widgetEvent->mMessage != eAfterPaint) {
    return NS_OK;
  }
  // Safe: we only listen for "MozAfterPaint" which dispatches NotifyPaintEvent.
  auto* paint = static_cast<NotifyPaintEvent*>(aEvent);
  uint64_t id = paint->TransactionId(SystemCallerGuarantee());
  if (id <= mSnapshot) {
    return NS_OK;
  }

  // Hold ourselves alive across detach: RemoveEventListener drops the
  // registry's strong ref, and our caller is the registry.
  RefPtr<FullscreenPaintBarrier> self(this);
  Detach();

  if (XRE_IsParentProcess()) {
    if (nsCOMPtr<nsIObserverService> os = services::GetObserverService()) {
      FULLSCREEN_LOG("Notifying observers of 'fullscreen-painted'");
      os->NotifyObservers(mWindow, "fullscreen-painted", nullptr);
    }
  } else {
    if (nsCOMPtr<nsIDocShell> ds = mWindow ? mWindow->GetDocShell() : nullptr) {
      if (BrowserChild* bc = BrowserChild::GetFrom(ds)) {
        FULLSCREEN_LOG("SendFullscreenPainted IPC sent");
        bc->SendFullscreenPainted(mIsEnter);
      }
    }
  }
  return NS_OK;
}

void FullscreenPaintBarrier::Detach() {
  if (!mAttached) {
    return;
  }
  mAttached = false;
  RefPtr<EventTarget> target =
      mWindow ? nsGlobalWindowInner::Cast(mWindow.get()) : nullptr;
  if (target) {
    target->RemoveEventListener(u"MozAfterPaint"_ns, this,
                                /* aUseCapture = */ false);
  }
}

}  // namespace mozilla::dom
