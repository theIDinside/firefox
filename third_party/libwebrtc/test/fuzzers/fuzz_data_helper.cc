/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/fuzzers/fuzz_data_helper.h"

#include <cstdint>

#include "api/array_view.h"

namespace webrtc {
namespace test {

FuzzDataHelper::FuzzDataHelper(rtc::ArrayView<const uint8_t> data)
    : data_(data) {}

}  // namespace test
}  // namespace webrtc
