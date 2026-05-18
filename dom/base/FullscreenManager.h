/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_FullscreenManager_h
#define mozilla_dom_FullscreenManager_h

#include <cstdint>

#include "mozilla/Maybe.h"
#include "mozilla/dom/BrowsingContext.h"
#include "nsPIDOMWindow.h"
#include "nsTArray.h"

class nsGlobalWindowOuter;

namespace mozilla::dom {

class CanonicalBrowsingContext;
class Document;
class EnterFullscreen;
class FullscreenManager;
class FullscreenServiceRequest;

class FullscreenServiceTimerObserver final : public nsIObserver,
                                             public nsINamed {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER
  NS_DECL_NSINAMED

  explicit FullscreenServiceTimerObserver(
      FullscreenServiceRequest* aRequestPayload);

 private:
  ~FullscreenServiceTimerObserver();
  RefPtr<FullscreenServiceRequest> mPayload;
};

class FullscreenServiceRequest {
 public:
  NS_INLINE_DECL_REFCOUNTING(FullscreenServiceRequest);
  FullscreenServiceRequest(FullscreenManager* aManager,
                           CanonicalBrowsingContext* aContext,
                           uint64_t aTransactionId);

  virtual void Run() = 0;
  virtual void OnFullscreenChange(Document* aChromeDoc,
                                  bool aEnteredFullscreen) = 0;
  virtual void Cancel() = 0;
  virtual void OnTransactionError() = 0;

  uint64_t TransactionId() const { return mTransactionId; }

  RefPtr<CanonicalBrowsingContext> GetBrowsingContext() const;
  bool IsRunning() const { return mRunning; }
  void SetRunning(bool aIsRunning);

  virtual void Resolve(nsresult aResult) = 0;
 protected:
  virtual ~FullscreenServiceRequest();

  void InitRequestTimer();

  FullscreenManager* mManager;
  bool mRunning = false;
  uint64_t mBrowsingContextId;
  uint64_t mTransactionId;
  nsCOMPtr<nsITimer> mTimer;
};

class EnterFullscreen final : public FullscreenServiceRequest {
 public:
  using Resolver = std::function<void(nsresult)>;
  EnterFullscreen(FullscreenManager* aManager,
                  CanonicalBrowsingContext* aContext, bool aKeyboardLock,
                  uint64_t aChildRequestId, Resolver&& aResolve)
      : FullscreenServiceRequest(aManager, aContext, aChildRequestId),
        mKeyboardLock(aKeyboardLock),
        mResolve(std::move(aResolve)) {}

  void Run() override;
  void OnFullscreenChange(Document* aChromeDoc,
                          bool aEnteredFullscreen) override;

  void OnTransactionError() override;
  void Cancel() override;

  bool mKeyboardLock;
  void Resolve(nsresult aResult) override;

 protected:
  ~EnterFullscreen() = default;

 private:
  bool RequestFullscreenTransition();
  void EnterFullscreenInContentProcesses();
  /**
   * After remote OOP ancestor frames has entered fullscreen, the request gets
   * committed to run for the requesting document sub tree.
   */
  void Commit();

  Resolver mResolve;
  // If this request failed/cancelled, and is waiting for browser fullscreen to
  // exit.
  bool mAwaitingChromeExit = false;
};

class ExitFullscreen final : public FullscreenServiceRequest {
 public:
  using Resolver = std::function<void(nsresult)>;
  ExitFullscreen(FullscreenManager* aManager,
                 CanonicalBrowsingContext* aContext, uint64_t aChildRequestId,
                 Resolver&& aResolve)
      : FullscreenServiceRequest(aManager, aContext, aChildRequestId),
        mResolve(std::move(aResolve)) {}

  void Run() override;
  void OnFullscreenChange(Document* aChromeDoc,
                          bool aEnteredFullscreen) override;

  void OnTransactionError() override;

  void Cancel() override;
  void Resolve(nsresult aResult) override;

 protected:
  ~ExitFullscreen() = default;

 private:
  void ExitBrowserFullscreen();
  void DispatchContentProcessWork(CanonicalBrowsingContext* aTopContext);

  Resolver mResolve;
  bool mRequestedExit = false;
  bool mAwaitingChromeExit = false;
};

class FullscreenManager {
 public:
  explicit FullscreenManager(uint64_t aWindowId);
  ~FullscreenManager();
  void QueueRequest(RefPtr<FullscreenServiceRequest> aRequest);
  void QueueEnterRequest(CanonicalBrowsingContext* aContext, bool aKeyboardLock,
                         uint64_t aChildRequestId,
                         EnterFullscreen::Resolver&& aResolve);
  void QueueExitRequest(CanonicalBrowsingContext* aContext,
                        uint64_t aChildRequestId,
                        ExitFullscreen::Resolver&& aResolve);

  // Request OS for application to go fullscreen
  void SetFullscreen(FullscreenReason aReason, bool aIsFullscreen);

  void SetFullscreen(nsPIDOMWindowOuter* aWindow, FullscreenReason aReason,
                     bool aIsFullscreen);

  void StartNextRequest();

  // Instruct all documents to exit out of DOM fullscreen, and exit browser
  // fullscreen.
  void CancelFullscreen();

  void OnFullscreenChanged(Document* aChromeDoc, bool isDOMFullscreen);
  void SetOngoingRequest();
  void SetFullscreenLeaf(CanonicalBrowsingContext* aContext);
  nsGlobalWindowOuter* GetWindow() const;
  RefPtr<CanonicalBrowsingContext> GetFullscreenLeaf() const;

  void OnRequestComplete(bool aStartNext);
  // Routes a FullscreenServiceTransaction tick to the currently-running
  // request after validating that aChildRequestId matches.
  void OnTransactionResponse(CanonicalBrowsingContext* aContext,
                             uint64_t aChildRequestId, nsresult aResult);

  uint64_t Id() const { return mWindowId; }

 private:
  uint64_t mWindowId;
  nsTArray<RefPtr<FullscreenServiceRequest>> mRequests;
  Maybe<uint64_t> mFullscreenedBrowsingContextLeaf = Nothing();
  RefPtr<FullscreenServiceRequest> mCurrentRequest = nullptr;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_FullscreenManager_h
