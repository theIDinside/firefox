// |reftest| shell-option(--enable-temporal) skip-if(!this.hasOwnProperty('Temporal')||!xulRuntime.shell) -- Temporal is not enabled unconditionally, requires shell-options
// Copyright (C) 2021 Kate Miháliková. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sup-temporal.instant.prototype.tolocalestring
description: >
    Conflicting properties of dateStyle must be rejected with a TypeError for the options argument
info: |
    Using sec-temporal-getdatetimeformatpattern:
    GetDateTimeFormatPattern ( dateStyle, timeStyle, matcher, opt, dataLocaleData, hc )

    1. If dateStyle is not undefined or timeStyle is not undefined, then
      a. For each row in Table 7, except the header row, do
        i. Let prop be the name given in the Property column of the row.
        ii. Let p be opt.[[<prop>]].
        iii. If p is not undefined, then
          1. Throw a TypeError exception.
features: [BigInt, Temporal]
---*/

// Table 14 - Supported fields + example value for each field
const conflictingOptions = [
  [ "weekday", "short" ],
  [ "era", "short" ],
  [ "year", "numeric" ],
  [ "month", "numeric" ],
  [ "day", "numeric" ],
  [ "hour", "numeric" ],
  [ "minute", "numeric" ],
  [ "second", "numeric" ],
  [ "dayPeriod", "short" ],
  [ "fractionalSecondDigits", 3 ],
];
const instant = new Temporal.Instant(957270896_987_650_000n);

assert.sameValue(typeof instant.toLocaleString("en", { dateStyle: "short" }), "string");
assert.sameValue(typeof instant.toLocaleString("en", { timeStyle: "short" }), "string");

for (const [ option, value ] of conflictingOptions) {
  assert.throws(TypeError, function() {
    instant.toLocaleString("en", { [option]: value, dateStyle: "short" });
  }, `instant.toLocaleString("en", { ${option}: "${value}",  dateStyle: "short" }) throws TypeError`);

  assert.throws(TypeError, function() {
    instant.toLocaleString("en", { [option]: value, timeStyle: "short" });
  }, `instant.toLocaleString("en", { ${option}: "${value}",  timeStyle: "short" }) throws TypeError`);

  // dateStyle or timeStyle present but undefined does not conflict
  instant.toLocaleString("en", { [option]: value, dateStyle: undefined });
  instant.toLocaleString("en", { [option]: value, timeStyle: undefined });
}

reportCompare(0, 0);
