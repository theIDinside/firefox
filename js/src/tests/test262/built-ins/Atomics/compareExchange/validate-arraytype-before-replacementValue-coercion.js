// |reftest| skip-if(!this.hasOwnProperty('Atomics')) -- Atomics is not enabled unconditionally
// Copyright (C) 2019 André Bargull. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-atomics.compareexchange
description: >
  TypedArray type is validated before `replacementValue` argument is coerced.
info: |
  24.4.4 Atomics.compareExchange ( typedArray, index, expectedValue, replacementValue )
    1. Let buffer be ? ValidateSharedIntegerTypedArray(typedArray).
    ...

  24.4.1.1 ValidateSharedIntegerTypedArray ( typedArray [ , onlyInt32 ] )
    ...
    4. Let typeName be typedArray.[[TypedArrayName]].
    5. If onlyInt32 is true, then
      a. If typeName is not "Int32Array", throw a TypeError exception.
    6. Else,
      a. If typeName is not "Int8Array", "Uint8Array", "Int16Array", "Uint16Array", "Int32Array",
         or "Uint32Array", throw a TypeError exception.
    ...
includes: [testTypedArray.js]
features: [Atomics, TypedArray]
---*/

var replacementValue = {
  valueOf() {
    throw new Test262Error("replacementValue coerced");
  }
};

for (var badArrayType of nonAtomicsFriendlyTypedArrayConstructors) {
  var typedArray = new badArrayType(new SharedArrayBuffer(8));
  assert.throws(TypeError, function() {
    Atomics.compareExchange(typedArray, 0, 0, replacementValue);
  });
}

reportCompare(0, 0);
