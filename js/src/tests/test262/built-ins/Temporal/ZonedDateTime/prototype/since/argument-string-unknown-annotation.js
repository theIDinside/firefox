// |reftest| shell-option(--enable-temporal) skip-if(!this.hasOwnProperty('Temporal')||!xulRuntime.shell) -- Temporal is not enabled unconditionally, requires shell-options
// Copyright (C) 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.zoneddatetime.prototype.since
description: Various forms of unknown annotation
features: [Temporal]
includes: [temporalHelpers.js]
---*/

const tests = [
  ["1970-01-01T00:00[UTC][foo=bar]", "with time zone"],
  ["1970-01-01T00:00[UTC][foo=bar][u-ca=iso8601]", "before calendar"],
  ["1970-01-01T00:00[UTC][u-ca=iso8601][foo=bar]", "after calendar"],
  ["1970-01-01T00:00[UTC][foo=bar][_foo-bar0=Ignore-This-999999999999]", "with another unknown annotation"],
];

const timeZone = "UTC";
const instance = new Temporal.ZonedDateTime(0n, timeZone);

tests.forEach(([arg, description]) => {
  const result = instance.since(arg);

  TemporalHelpers.assertDuration(
    result,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    `unknown annotation (${description})`
  );
});

reportCompare(0, 0);
