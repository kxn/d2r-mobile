# Store-demand use frontiers

This representation changes no NVN command, ready/dirty check, hazard, resource
pin, query liveness or depth reverse proof. `store_index_query` is unchanged.
Within one index build, consecutive events in a bucket with identical READ/ATT
kind and view/flags keep their last sequence. Any different event in that
bucket ends the run. Every color/depth-stencil clear, including a clear of a
possible alias, ends every bucket's epoch.

A false query answer requires a full-overwrite clear witness c. No compressed
use run crosses c. For every query start s, an original use exists in [s,c] iff
the last use of the run is in [s,c]. READ/ATT, alias and global events only
require preservation. This preserves the Boolean answer, not earliest-use
sequence identity. Views must match for own-bucket membership; aliases still
consider all events. Bucket order remains sorted because a tail cannot move
past a later event in that same bucket.

The existing bucket `last_read` uint32 becomes a tail event index in compact
mode. Bucket/event layouts remain 40/16 bytes. Added global state is 12 bytes
(two int policies and a uint32 clear sequence on the supported targets), with
one cached environment lookup and one atomic policy load per index build.
`DM_VULKAN_STORE_FRONTIER_REFERENCE=1` selects the exact previous representation.
Normal operation uses compact representation. There is no cross-submission
cache. Nonmergeable events pay the tail/attribute checks; uncommon same-sequence
READ/ATT/READ interleaving can emit an extra equivalent READ versus the old
same-sequence-only dedup. The oracle explicitly covers this case. `recorded`
counts stored index records after compression, not actual draws/reads removed.

Actual f75 six-FP planning parent is 4.855–6.365% of each window's NvN user
period. Forward store semantic work is 2.385–2.940%; finalization/scatter/alias
closure is .554–.829%. This candidate only touches event construction and
scatter within those envelopes; shader/descriptor lookup, qsort, alias closure,
walk validation and reverse proof remain. Ten original brackets have
14223–14272 original events/guest. Append+scatter theoretically writes 32 bytes
per record (about 445 KiB/guest); this is neither measured DRAM traffic nor a
CPU/FPS prediction. Real compact opportunity in the game is not measured yet.

Host qualification uses all query starts (including run interiors), mip/layer
and all three planes, overlapping serials, clears of aliases, full/partial
masks/scissors, same-sequence READ/ATT interleaving, globals and failed growth.
The original all-event byte oracle runs with reference representation only.
The original GPU store-overwrite fixtures run compact representation with the
independent original raw-command `store_scan` comparison and Core/SYNC checks.
Host GPU results are not Android/game acceptance.
