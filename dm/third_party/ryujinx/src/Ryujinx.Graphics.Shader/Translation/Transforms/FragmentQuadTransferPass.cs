using Ryujinx.Graphics.Shader.IntermediateRepresentation;
using Ryujinx.Graphics.Shader.Translation.Optimizations;
using System;
using System.Collections.Generic;
using System.Linq;
using static Ryujinx.Graphics.Shader.IntermediateRepresentation.OperandHelper;

namespace Ryujinx.Graphics.Shader.Translation.Transforms
{
    // Explicit device profile only. No transformation is performed in Metal or
    // the normal Vulkan profile. Every accepted source is cloned at entry,
    // before all branches/discards; no derivative is inserted at the old call.
    static class FragmentQuadTransferPass
    {
        public static void Run(TransformContext context)
        {
            if (!context.GpuAccessor.QueryHostFragmentQuadTransfer() ||
                context.Stage != ShaderStage.Fragment || context.TargetLanguage != TargetLanguage.Spirv)
                return;

            List<(LinkedListNode<INode> Node, Operation Op, int Axis)> candidates = new();
            foreach (BasicBlock block in context.Blocks)
            for (var node = block.Operations.First; node != null; node = node.Next)
            {
                if (node.Value is not Operation op) continue;
                Instruction inst = op.Inst & Instruction.Mask;
                if (inst is not (Instruction.Shuffle or Instruction.ShuffleDown or Instruction.ShuffleUp or Instruction.ShuffleXor))
                    continue;
                if (inst != Instruction.ShuffleXor || op.SourcesCount != 3 || op.DestsCount != 2 ||
                    op.GetSource(1).Type != OperandType.Constant || op.GetSource(1).Value is not (1 or 2) ||
                    op.GetSource(2).Type != OperandType.Constant || op.GetSource(2).Value != 0x1c03)
                    throw new InvalidOperationException("quad_transfer_v1: unsupported fragment shuffle mode/index/clamp");
                try { ValidateInput(op.GetSource(0), new HashSet<Operand>(), 0); }
                catch (InvalidOperationException) when (context.GpuAccessor.QueryHostFragmentQuadReadonlySources())
                { FragmentQuadReadonlySources.Validate(context, op.GetSource(0)); }
                candidates.Add((node, op, op.GetSource(1).Value));
            }
            if (candidates.Count == 0) return;
            if (!context.IsEntryFunction || context.Blocks[0].Predecessors.Count != 0)
                throw new InvalidOperationException("quad_transfer_v1: primitive-uniform entry cannot be proven for a subroutine or entry backedge");

            List<Operation> prefix = new();
            Dictionary<Operand, Operand> inputs = new();
            Dictionary<(Operand, int), Operand> transfers = new();
            Dictionary<int, Operand> coordinates = new();
            foreach (var (node, op, axis) in candidates)
            {
                Operand original = op.GetSource(0);
                if (!transfers.TryGetValue((original, axis), out Operand result))
                {
                    Operand value = context.GpuAccessor.QueryHostFragmentQuadReadonlySources()
                        ? FragmentQuadReadonlySources.Clone(context, original, inputs, prefix)
                        : CloneInput(original, inputs, prefix);
                    if (value.Type == OperandType.Constant) result = value;
                    else
                    {
                        if (!coordinates.TryGetValue(axis, out Operand coord))
                        {
                            context.UsedFeatures |= FeatureFlags.FragCoordXY;
                            coord = Local();
                            prefix.Add(new Operation(Instruction.Load, StorageKind.Input, coord,
                                Const((int)IoVariable.FragmentCoord), Const(axis == 1 ? 0 : 1)));
                            coordinates.Add(axis, coord);
                        }
                        result = Local();
                        prefix.Add(new Operation(Instruction.QuadTransfer, axis, result, value, coord) { ForcePrecise = true });
                        context.UsedFeatures |= FeatureFlags.FragmentQuadTransfer;
                    }
                    transfers.Add((original, axis), result);
                }
                Operand dest = op.GetDest(0), valid = op.GetDest(1);
                // 0x1c03 confines each lane to its group of four; XOR1/2 can
                // never escape it, so the original valid predicate is true.
                node.List.AddBefore(node, new Operation(Instruction.Copy, dest, result));
                node.List.AddBefore(node, new Operation(Instruction.Copy, valid, Const(IrConsts.True)));
                Utils.DeleteNode(node, op);
            }
            // Reverse insertion keeps all source definitions before their use.
            for (int i = prefix.Count - 1; i >= 0; --i)
                context.Blocks[0].Operations.AddFirst(prefix[i]);
        }

        private static void ValidateInput(Operand value, HashSet<Operand> seen, int depth)
        {
            if (value.Type == OperandType.Constant) return;
            if (depth > 16 || !seen.Add(value) || value.Type != OperandType.LocalVariable ||
                value.AsgOp is not Operation op || op.DestsCount != 1)
                throw new InvalidOperationException("quad_transfer_v1: source is not an acyclic pure input");
            if (op.Inst == Instruction.Copy && op.SourcesCount == 1)
            {
                ValidateInput(op.GetSource(0), seen, depth + 1);
                seen.Remove(value);
                return;
            }
            if (op.Inst != Instruction.Load || op.StorageKind != StorageKind.Input ||
                op.SourcesCount != 3 || op.GetSource(0).Type != OperandType.Constant ||
                (IoVariable)op.GetSource(0).Value != IoVariable.UserDefined ||
                op.GetSource(1).Type != OperandType.Constant || op.GetSource(1).Value < 0 ||
                op.GetSource(2).Type != OperandType.Constant || (uint)op.GetSource(2).Value > 3)
                throw new InvalidOperationException("quad_transfer_v1: source must be a scalar constant-index input (no memory, arithmetic, phi or call)");
            seen.Remove(value);
        }

        private static Operand CloneInput(Operand value, Dictionary<Operand, Operand> cache, List<Operation> prefix)
        {
            if (value.Type == OperandType.Constant) return value;
            if (cache.TryGetValue(value, out Operand found)) return found;
            Operation op = (Operation)value.AsgOp;
            if (op.Inst == Instruction.Copy)
            {
                found = CloneInput(op.GetSource(0), cache, prefix);
            }
            else
            {
                Operand[] sources = Enumerable.Range(0, op.SourcesCount).Select(op.GetSource).ToArray();
                found = Local();
                prefix.Add(new Operation(Instruction.Load, StorageKind.Input, found, sources));
            }
            cache.Add(value, found);
            return found;
        }
    }
}
