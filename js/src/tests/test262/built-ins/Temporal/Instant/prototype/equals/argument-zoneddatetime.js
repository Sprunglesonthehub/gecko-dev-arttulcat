// |reftest| shell-option(--enable-temporal) skip-if(!this.hasOwnProperty('Temporal')||!xulRuntime.shell) -- Temporal is not enabled unconditionally, requires shell-options
// Copyright (C) 2021 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.instant.prototype.until
description: Fast path for converting Temporal.ZonedDateTime to Temporal.Instant
info: |
    sec-temporal.instant.prototype.until step 3:
      3. Set _other_ to ? ToTemporalInstant(_other_).
    sec-temporal-totemporalinstant step 1.b:
      b. If _item_ has an [[InitializedTemporalZonedDateTime]] internal slot, then
        i. Return ! CreateTemporalInstant(_item_.[[Nanoseconds]]).
includes: [compareArray.js, temporalHelpers.js]
features: [Temporal]
---*/

TemporalHelpers.checkToTemporalInstantFastPath((datetime) => {
  const instant = new Temporal.Instant(1_000_000_000_987_654_321n);
  assert(instant.equals(datetime));
});

reportCompare(0, 0);
