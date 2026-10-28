from pathlib import Path
import hashlib
O=Path(__file__).resolve().parent;BASE=O.parent/'turnip-internal-queue-sync-v1/mesa-26.2.5';p=BASE/'src/freedreno/ir3/ir3_sched.c';s=p.read_text()
def replace(old,new):
 global s
 assert s.count(old)==1,(old[:90],s.count(old));s=s.replace(old,new)
replace('struct ir3_sched_ctx {','''#ifndef DM_IR3_MEMORY_CRITICAL_SCHED
#define DM_IR3_MEMORY_CRITICAL_SCHED 0
#endif
#if DM_IR3_MEMORY_CRITICAL_SCHED != 0 && DM_IR3_MEMORY_CRITICAL_SCHED != 1
#error "DM_IR3_MEMORY_CRITICAL_SCHED must be 0 or 1"
#endif

struct ir3_sched_ctx {''')
replace('   unsigned ip;','''   unsigned ip;
#if DM_IR3_MEMORY_CRITICAL_SCHED
   bool critical_policy;
   unsigned remaining_inputs;
#endif''')
replace('   unsigned max_delay;','''   unsigned max_delay;
#if DM_IR3_MEMORY_CRITICAL_SCHED
   /* Read depth and provenance use real SSA data edges only. All original
    * dependency/hazard edges remain in the scheduling DAG unchanged. */
   uint8_t memory_depth;
   bool memory_ancestor;
#endif''')
anchor='#define foreach_sched_node(__n, __list)'
pos=s.index(anchor)
helper='''#if DM_IR3_MEMORY_CRITICAL_SCHED
/* Metadata only: never remove a dependency or classify a write as a read. */
static bool
critical_read(struct ir3_instruction *instr)
{
   return instr->dsts_count && !has_dummy_dst(instr) &&
      !is_store(instr) && !is_atomic(instr->opc) &&
      ((is_tex(instr) && !is_tex_shuffle(instr)) ||
       (is_load(instr) && !is_local_mem_load(instr)));
}

static bool
critical_real_edge(struct ir3_instruction *use,
                   struct ir3_instruction *producer)
{
   if (use->block != producer->block || use->opc == OPC_META_PHI)
      return false;
   foreach_ssa_src_n (src, i, use) {
      if (src == producer && !__is_false_dep(use, i))
         return true;
   }
   return false;
}

static bool
critical_chain_node(struct ir3_sched_ctx *ctx, struct ir3_sched_node *n)
{
   if (!ctx->critical_policy || ctx->remaining_inputs)
      return false;
   /* Before the first read require at least two dependent reads. After a
    * real read retain priority through the final dependent-read address. */
   return n->memory_depth >= 2 ||
      (n->memory_depth && (critical_read(n->instr) || n->memory_ancestor));
}
#endif

'''
s=s[:pos]+helper+s[pos:]
replace('      child->earliest_ip = MAX2(child->earliest_ip, ctx->ip + delay);','''      child->earliest_ip = MAX2(child->earliest_ip, ctx->ip + delay);
#if DM_IR3_MEMORY_CRITICAL_SCHED
      if (ctx->critical_policy &&
          (critical_read(instr) || n->memory_ancestor) &&
          critical_real_edge(child->instr, instr))
         child->memory_ancestor = true;
#endif''')
replace('   instr->flags |= IR3_INSTR_MARK;','''#if DM_IR3_MEMORY_CRITICAL_SCHED
   if (ctx->critical_policy && is_input(instr)) {
      assert(ctx->remaining_inputs > 0);
      ctx->remaining_inputs--;
   }
#endif
   instr->flags |= IR3_INSTR_MARK;''')
replace('''      if (sched_check_src_cond(instr, is_outstanding_sy, ctx))
         return true;''','''      if (sched_check_src_cond(instr, is_outstanding_sy, ctx)) {
#if DM_IR3_MEMORY_CRITICAL_SCHED
         /* Only waive batching preference, never a DAG/hardware delay, ss
          * dependency, kill rule, address-register rule, or queue-size cap. */
         if (!critical_chain_node(ctx, instr->data))
#endif
            return true;
      }''')
anchor='static struct ir3_instruction *\nchoose_instr(struct ir3_sched_ctx *ctx, struct ir3_sched_notes *notes)'
pos=s.index(anchor)
chooser='''#if DM_IR3_MEMORY_CRITICAL_SCHED
static struct ir3_sched_node *
choose_memory_critical(struct ir3_sched_ctx *ctx, struct ir3_sched_notes *notes)
{
   struct ir3_sched_node *chosen = NULL;
   if (!ctx->critical_policy || ctx->remaining_inputs)
      return NULL;
   foreach_sched_node (n, &ctx->dag->heads) {
      if (is_meta(n->instr) || n->output ||
          !critical_chain_node(ctx, n) || node_delay(ctx, n) ||
          should_defer(ctx, n->instr) || !check_instr(ctx, notes, n->instr))
         continue;
      if (!chosen || n->memory_depth > chosen->memory_depth ||
          (n->memory_depth == chosen->memory_depth &&
           live_effect(n->instr) < live_effect(chosen->instr)) ||
          (n->memory_depth == chosen->memory_depth &&
           live_effect(n->instr) == live_effect(chosen->instr) &&
           n->max_delay > chosen->max_delay))
         chosen = n;
   }
   return chosen;
}
#endif

'''
s=s[:pos]+chooser+s[pos:]
replace('''   chosen = choose_instr_dec(ctx, notes, true);''','''#if DM_IR3_MEMORY_CRITICAL_SCHED
   chosen = choose_memory_critical(ctx, notes);
   if (chosen)
      return chosen->instr;
#endif

   chosen = choose_instr_dec(ctx, notes, true);''')
replace('''      max_delay = MAX2(child->max_delay, max_delay);''','''      max_delay = MAX2(child->max_delay, max_delay);
#if DM_IR3_MEMORY_CRITICAL_SCHED
      struct ir3_sched_ctx *ctx = state;
      if (ctx->critical_policy && critical_real_edge(child->instr, n->instr)) {
         unsigned depth = child->memory_depth + critical_read(child->instr);
         n->memory_depth = MAX2(n->memory_depth, MIN2(depth, 8));
      }
#endif''')
replace('dag_traverse_bottom_up(ctx->dag, sched_dag_max_delay_cb, NULL);','dag_traverse_bottom_up(ctx->dag, sched_dag_max_delay_cb, ctx);')
replace('''   ctx->block = block;''','''   ctx->block = block;
#if DM_IR3_MEMORY_CRITICAL_SCHED
   /* Structural pilot scope: FS first non-preamble block only. No shader,
    * resource, texture, material or scene identity participates. */
   ctx->critical_policy = block->shader->type == MESA_SHADER_FRAGMENT &&
      block == ir3_after_preamble(block->shader) && !block->in_early_preamble;
   ctx->remaining_inputs = 0;
   if (ctx->critical_policy) {
      foreach_instr (instr, &block->instr_list)
         ctx->remaining_inputs += is_input(instr);
   }
#endif''')
(O/'scheduler-candidate.c').write_text(s)
print(hashlib.sha256(s.encode()).hexdigest())
