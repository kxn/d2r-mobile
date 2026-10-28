# Submission-local attachment-domain Store read filtering

Experimental default **OFF**, process-fixed `DM_VULKAN_STORE_ATTACHMENT_DOMAIN=0|1`.
Only the Store proof's Shader `IX_READ` events are filtered. Guest replay, DS/query/state plans, copies, unknown/global requirements, resource dependencies and texture formats remain unchanged.

The domain is the initial logical `S.targets` plus every reachable SET_RT in the existing forward walk, across CALL/JUMP/roots. It only grows. A valid Store query concerns an attachment that was enrolled before its physical pass; its start ordinal is therefore no earlier than enrollment. A read before enrollment cannot affect that later query. Old targets remain enrolled after switching RT, since switching need not end the old pass. Pixel-local anticipates future physical attachments but preserves their stores, and ghost restores only an already-captured draw target. A source is filtered only if its known footprint is disjoint from all enrolled targets. Native residency is not used as a read-only or non-alias proof.

Existing descriptor lookup and bucket footprints are retained. Per bucket, a uint32 proof records membership/positive relevance and how many domain targets a negative proof checked. New targets invalidate only negative proofs; their next use compares only newly added targets. Positive proofs remain valid until the whole index ends. Added metadata is 16KiB proof +8KiB target indices, reused, and only ON clears the16KiB. Every considered read adds a cheap branch/proof access and local work counters. OFF retains a branch and fixed per-build setup, not literally zero overhead. No new full command walk or per-read scan of all prior RTs is added. Descriptor resolution, Shader stage/slot parsing and sorting of existing footprint buckets are **not** eliminated.

`vk_store_attachment_domain_stats(out,&enabled)` publishes cumulative work counts in this order: targets, considered, skipped, negative_rechecks, footprint_comparisons, positive_hits. They are work counts, not timing. Candidate counters accumulate locally and publish once in Store finalization; no per-read atomics or clock reads. Unknown descriptors still produce global_need; unknown footprint address/size remains relevant. Descriptor epoch invalidation and the existing complete reference fallback remain authoritative. Nonzero handles always retain the original known_zero semantics.

Run from repository root:

```bash
python3 scripts/dev/experiments/store-attachment-domain/check_host.py --output .local/logs/store-attachment-domain-v1/host-final
```

This compiles the actual production walk/reverse/Store source with GCC ASan+UBSan and metadata fixtures. OFF uses the existing whole-index equality oracle. ON compares every ever-enrolled target at each legitimate sequence/start/done/plane against the frozen full interpreter. The candidate bucket/hash/events/sorted/globals/aliases/descriptor cache/count/epoch/statistics are then restored so subsequent GPU fixtures can actually consume the filtered candidate. That snapshot machinery is host-test-only.

The11 scenarios cover a pre-first-RT read, negative-cache revisit after a new alias target, switching away while the old target may still be queried, literal-zero vs irrelevant reads with unknown Shader stages, initial targets/siblings, nested CALL inheritance, clear/read ordering, unknown descriptors, unchanged conservative copies, submission reset, and actual epoch fallback. Ghost is represented by the stricter superset of all previously enrolled targets, rather than a fake UI replay. No runtime ghost/GPU content or Android game validation is claimed by the host oracle.

Three isolated unsafe source mutations must fail with an actual Store-decision mismatch: keep a stale negative after target growth; forget old targets on SET_RT; learn known_zero from an irrelevant nonzero handle. Cold default0/explicit0/1/invalid and no re-reading after first selection are checked. The main runtime is not mutated by these negative controls. Compiler/source dependency hashes and all logs are saved under the requested output directory.

Current formal PMU gives a2.460M instruction/Surface envelope for forward Shader/texture read collection plus .267M for Store prefix/scatter. It includes retained parsing/descriptor work, so is not predicted savings. Actual reduction and newly added proof management require same-binary work counters and official whole-profile measurements. Real GPU/content/device verification remains pending for this candidate.
