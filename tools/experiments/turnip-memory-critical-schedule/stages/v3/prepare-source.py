"""Fill proven critical-read hard-delay slots with legal ALU work before unrelated tex."""
from pathlib import Path
import json,hashlib
R=Path('/home/dl/d2r-mobile');O=Path(__file__).resolve().parent;B=R/'.local/build/turnip-memory-critical-schedule-v2/mesa-26.2.5';f='src/freedreno/ir3/ir3_postsched.c';s=(B/f).read_text();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
old='''      if (chosen) {
         di(chosen->instr, "csp: chose (real-SSA memory chain, hard ready)");
         return chosen->instr;
      }
   }
#endif'''
new='''      if (chosen) {
         di(chosen->instr, "csp: chose (real-SSA memory chain, hard ready)");
         return chosen->instr;
      }

      /* A qualified read may already be a DAG head but still need ALU delay
       * slots for its address/UV producer. Unrelated tex doesn't advance the
       * legalize ALU cycle and only extends the later SY producer group.
       * Prefer useful, wait-free ordinary ALU work to fill that hard gap.
       * Never add nops, skip a hard delay, or override physical dependencies.
       * If no such work exists, preserve the original fallback policy. */
      bool waiting_read = false;
      foreach_sched_node (n, &ctx->dag->heads) {
         if (n->instr->dm_memory_chain_priority &&
             is_sy_producer(n->instr) && node_delay(ctx, n)) {
            waiting_read = true;
            break;
         }
      }
      if (waiting_read) {
         foreach_sched_node (n, &ctx->dag->heads) {
            if (!is_alu(n->instr) ||
                is_scalar_alu(n->instr, ctx->v->compiler) ||
                is_input(n->instr) || node_delay(ctx, n) ||
                (n->instr->flags & (IR3_INSTR_SS | IR3_INSTR_SY)))
               continue;
            if (!chosen || n->max_delay > chosen->max_delay)
               chosen = n;
         }
         if (chosen) {
            di(chosen->instr, "csp: chose (ALU fills critical-read hard gap)");
            return chosen->instr;
         }
      }
   }
#endif'''
assert s.count(old)==1;p=O/'candidate-files'/f;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(s.replace(old,new));(O/'candidate-source-inputs.json').write_text(json.dumps({'files':{f:{'before':sha(B/f),'candidate':sha(p)}},'scope':'Only existing postRA choice changes in pilot block. When real-SSA-qualified read is DAG head with hard-delay>0, choose another hard-ready ordinary vector ALU without SS/SY to fill actual legalize cycles before original noncritical tex preference. No DAG/dependency/sync/delay changes, no nops or forced idle, fallback unchanged.'},indent=2)+'\n')
