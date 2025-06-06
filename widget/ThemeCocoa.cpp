/* -*- Mode: C++; tab-width: 40; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ThemeCocoa.h"
#include "gfxPlatform.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/ServoStyleConsts.h"

namespace mozilla::widget {

void ThemeCocoa::DrawWidgetBackground(gfxContext* aContext, nsIFrame* aFrame,
                                      StyleAppearance aAppearance,
                                      const nsRect& aRect,
                                      const nsRect& aDirtyRect,
                                      DrawOverflow aDrawOverflow) {
  switch (aAppearance) {
    case StyleAppearance::Tooltip:
      // Cocoa tooltip background and border are already drawn by the
      // OS window server.
      return;
    default:
      break;
  }
  return Theme::DrawWidgetBackground(aContext, aFrame, aAppearance, aRect,
                                     aDirtyRect, aDrawOverflow);
}

bool ThemeCocoa::CreateWebRenderCommandsForWidget(
    mozilla::wr::DisplayListBuilder& aBuilder,
    mozilla::wr::IpcResourceUpdateQueue& aResources,
    const mozilla::layers::StackingContextHelper& aSc,
    mozilla::layers::RenderRootStateManager* aManager, nsIFrame* aFrame,
    StyleAppearance aAppearance, const nsRect& aRect) {
  switch (aAppearance) {
    case StyleAppearance::Tooltip:
      // Cocoa tooltip background and border are already drawn by the
      // OS window server.
      return true;
    default:
      break;
  }
  return Theme::CreateWebRenderCommandsForWidget(
      aBuilder, aResources, aSc, aManager, aFrame, aAppearance, aRect);
}

}  // namespace mozilla::widget
