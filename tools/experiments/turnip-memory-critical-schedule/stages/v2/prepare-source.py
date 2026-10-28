"""Construct persistent real-SSA critical-chain metadata and post-RA priority pilot."""
from pathlib import Path
import json,hashlib,difflib
R=Path('/home/dl/d2r-mobile');O=Path(__file__).resolve().parent;B=R/'.local/build/turnip-memory-critical-schedule-v1/mesa-26.2.5';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
files=['src/freedreno/ir3/ir3.h','src/freedreno/ir3/ir3_sched.c','src/freedreno/ir3/ir3_postsched.c'];texts={f:(B/f).read_text()for f in files}
f=files[0];s=texts[f];old='   uint8_t repeat;\n   uint8_t nop;';assert s.count(old)==1;s=s.replace(old,old+'''
   /* Pilot metadata from real pre-RA SSA read dependencies. 0 means unmarked.
    * This is not a hardware flag or per-pass data: it survives RA and clones.
    * Fresh copy/spill instructions stay zero and retain normal scheduling. */
   uint8_t dm_memory_chain_priority;''');texts[f]=s
f=files[1];s=texts[f];old='   struct ir3_sched_node *n = instr->data;\n\n   /* If this instruction';assert s.count(old)==1;s=s.replace(old,'''   struct ir3_sched_node *n = instr->data;
#if DM_IR3_MEMORY_CRITICAL_SCHED
   /* Record only the front-end's existing real-SSA eligibility. Do not derive
    * this again from post-RA false/WAR/WAW dependencies. The +1 keeps zero
    * reserved for instructions that were not qualified by this policy. */
   instr->dm_memory_chain_priority =
      critical_chain_node(ctx, n) ? n->memory_depth + 1 : 0;
#endif

   /* If this instruction''');texts[f]=s
f=files[2];s=texts[f];needle='struct ir3_postsched_ctx {';assert s.count(needle)==1;s=s.replace(needle,'''#ifndef DM_IR3_MEMORY_CRITICAL_SCHED
#define DM_IR3_MEMORY_CRITICAL_SCHED 0
#endif
#if DM_IR3_MEMORY_CRITICAL_SCHED != 0 && DM_IR3_MEMORY_CRITICAL_SCHED != 1
#error "DM_IR3_MEMORY_CRITICAL_SCHED must be 0 or 1"
#endif

'''+needle)
needle='   /* Next prioritize expensive instructions: */';assert s.count(needle)==1;s=s.replace(needle,'''#if DM_IR3_MEMORY_CRITICAL_SCHED
   /* Honor the first FS main block's pre-RA real-SSA read-chain priority.
    * The physical dependency graph is unchanged; only DAG heads qualify.
    * All required sync flags and hard delays above remain authoritative.
    * A SY/SS soft delay is a latency-hiding preference, not a legality rule:
    * advancing a critical consumer still retains its required SY/SS wait.
    * Inputs, meta, and hard-ready kill/demote keep their original priority. */
   if (ctx->ir->type == MESA_SHADER_FRAGMENT &&
       ctx->block == ir3_after_preamble(ctx->ir) &&
       !ctx->block->in_early_preamble) {
      foreach_sched_node (n, &ctx->dag->heads) {
         if (!n->instr->dm_memory_chain_priority || node_delay(ctx, n) ||
             is_meta(n->instr) || is_input(n->instr) ||
             is_kill_or_demote(n->instr))
            continue;
         if (!chosen ||
             n->instr->dm_memory_chain_priority >
                chosen->instr->dm_memory_chain_priority ||
             (n->instr->dm_memory_chain_priority ==
                 chosen->instr->dm_memory_chain_priority &&
              n->max_delay > chosen->max_delay))
            chosen = n;
      }
      if (chosen) {
         di(chosen->instr, "csp: chose (real-SSA memory chain, hard ready)");
         return chosen->instr;
      }
   }
#endif

'''+needle);texts[f]=s
for f,t in texts.items():
 p=O/'candidate-files'/f;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(t)
(O/'candidate-source-inputs.json').write_text(json.dumps({'files':{f:{'before':sha(B/f),'candidate':sha(O/'candidate-files'/f)}for f in files},'scope':'Only persistent real-SSA metadata and existing post-RA selection. No dependencies, sync computation, delay calculation, kill handling, emission, layouts, shader input or runtime changes.'},indent=2)+'\n')
