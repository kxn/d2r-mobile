using Ryujinx.Graphics.Shader.IntermediateRepresentation;
using System.Collections.Generic;
using System.Linq;

using static Ryujinx.Graphics.Shader.StructuredIr.AstHelper;

namespace Ryujinx.Graphics.Shader.StructuredIr
{
    static class AstOptimizer
    {
        public static void Optimize(StructuredProgramContext context)
        {
            AstBlock mainBlock = context.CurrentFunction.MainBlock;

            // When debug mode is enabled, we disable expression propagation
            // (this makes comparison with the disassembly easier).
            if (!context.DebugMode)
            {
                AstBlockVisitor visitor = new(mainBlock);

                foreach (IAstNode node in visitor.Visit())
                {
                    if (node is AstAssignment assignment && assignment.Destination is AstOperand propVar)
                    {
                        // SPIR-V function arguments use Function pointers.
                        // Keep calls and all address-taken input/output locals.
                        if (IsAddressTaken(propVar) ||
                            !(IsStableExpression(assignment.Source) || CanMoveToSingleUse(assignment, propVar) ||
                              CanInlineAdjacentUniformRead(assignment, propVar)))
                        {
                            continue;
                        }

                        bool isWorthPropagating = propVar.Uses.Count == 1 || IsWorthPropagating(assignment.Source);

                        if (propVar.Defs.Count == 1 && isWorthPropagating)
                        {
                            PropagateExpression(propVar, assignment.Source);
                        }

                        if (propVar.Type == OperandType.LocalVariable && propVar.Uses.Count == 0)
                        {
                            visitor.Block.Remove(assignment);

                            context.CurrentFunction.Locals.Remove(propVar);
                        }
                    }
                }
            }

            RemoveEmptyBlocks(mainBlock);
        }

        private static bool IsAddressTaken(AstOperand operand)
        {
            return operand.Uses.Concat(operand.Defs).Any(n =>
                n is AstOperation operation && (operation.Inst & Instruction.Mask) == Instruction.Call);
        }

        // Only substitute a uniform read into the very next pure assignment.
        // Unlike unrestricted memory propagation, this cannot cross a call,
        // branch, memory write, barrier, or even another AST statement. The
        // single consumer also prevents duplicated loads. Other memory kinds
        // keep their original evaluation point.
        private static bool CanInlineAdjacentUniformRead(AstAssignment assignment, AstOperand variable)
        {
            if (variable.Uses.Count != 1 ||
                assignment.Source is not AstOperation read ||
                (read.Inst & Instruction.Mask) != Instruction.Load ||
                read.StorageKind != StorageKind.ConstantBuffer ||
                assignment.LLNode?.Next?.Value is not AstAssignment consumer ||
                !ContainsExpression(consumer.Source, variable.Uses.First()) ||
                CountOccurrences(consumer.Source, variable) != 1 ||
                !IsStableExpression(consumer.Source)) return false;

            HashSet<AstOperand> indices = new();
            for (int index = 0; index < read.SourcesCount; index++)
            {
                if (!IsStableExpression(read.GetSource(index), intervalReads: indices)) return false;
            }
            return !indices.Contains(variable) && !ClobbersRead(consumer.Source, indices);
        }

        // Uses counts consuming nodes, not operand positions: x*x has one
        // consuming node but would duplicate a propagated memory read.
        private static int CountOccurrences(IAstNode node, AstOperand variable, int depth = 0)
        {
            if (depth > 128) return 2;
            if (node == variable) return 1;
            int count = 0;
            if (node is AstOperation operation)
            {
                for (int index = 0; index < operation.SourcesCount && count < 2; index++)
                    count += CountOccurrences(operation.GetSource(index), variable, depth + 1);
            }
            return count;
        }

        // Expression propagation moves evaluation to the consumer. A captured
        // read of a mutable local/call argument must keep its original position.
        // Quad/subgroup operations and memory accesses also retain execution
        // context; unknown operations are deliberately not propagated.
        private static bool IsStableExpression(IAstNode node, int depth = 0, HashSet<AstOperand> intervalReads = null)
        {
            if (depth > 128) return false;
            if (node is AstOperand operand)
            {
                if (intervalReads != null && operand.Type == OperandType.LocalVariable)
                {
                    intervalReads.Add(operand);
                    return true;
                }
                return operand.Type == OperandType.Constant ||
                    (operand.Type == OperandType.LocalVariable && operand.Defs.Count == 1 && !IsAddressTaken(operand));
            }
            if (node is not AstOperation operation) return false;

            bool pure = (operation.Inst & Instruction.Mask) switch
            {
                Instruction.Absolute or Instruction.Add or Instruction.BitCount or
                Instruction.BitfieldExtractS32 or Instruction.BitfieldExtractU32 or Instruction.BitfieldInsert or
                Instruction.BitfieldReverse or Instruction.BitwiseAnd or Instruction.BitwiseExclusiveOr or
                Instruction.BitwiseNot or Instruction.BitwiseOr or Instruction.Ceiling or Instruction.Clamp or
                Instruction.ClampU32 or Instruction.CompareEqual or Instruction.CompareGreater or
                Instruction.CompareGreaterOrEqual or Instruction.CompareGreaterOrEqualU32 or Instruction.CompareGreaterU32 or
                Instruction.CompareLess or Instruction.CompareLessOrEqual or Instruction.CompareLessOrEqualU32 or
                Instruction.CompareLessU32 or Instruction.CompareNotEqual or Instruction.ConditionalSelect or
                Instruction.ConvertFP32ToFP64 or Instruction.ConvertFP64ToFP32 or Instruction.ConvertFP32ToS32 or
                Instruction.ConvertFP32ToU32 or Instruction.ConvertFP64ToS32 or Instruction.ConvertFP64ToU32 or
                Instruction.ConvertS32ToFP32 or Instruction.ConvertS32ToFP64 or Instruction.ConvertU32ToFP32 or
                Instruction.ConvertU32ToFP64 or Instruction.Copy or Instruction.Cosine or Instruction.Divide or
                Instruction.ExponentB2 or Instruction.FindLSB or Instruction.FindMSBS32 or Instruction.FindMSBU32 or
                Instruction.Floor or Instruction.FusedMultiplyAdd or Instruction.IsNan or Instruction.LogarithmB2 or
                Instruction.LogicalAnd or Instruction.LogicalExclusiveOr or Instruction.LogicalNot or Instruction.LogicalOr or
                Instruction.Maximum or Instruction.MaximumU32 or Instruction.Minimum or Instruction.MinimumU32 or
                Instruction.Modulo or Instruction.Multiply or Instruction.MultiplyHighS32 or Instruction.MultiplyHighU32 or
                Instruction.Negate or Instruction.PackDouble2x32 or Instruction.PackHalf2x16 or
                Instruction.ReciprocalSquareRoot or Instruction.Round or Instruction.ShiftLeft or Instruction.ShiftRightS32 or
                Instruction.ShiftRightU32 or Instruction.Sine or Instruction.SquareRoot or Instruction.Subtract or
                Instruction.Truncate or Instruction.UnpackDouble2x32 or Instruction.UnpackHalf2x16 or
                Instruction.VectorExtract => true,
                _ => false,
            };
            if (!pure) return false;
            for (int index = 0; index < operation.SourcesCount; index++)
            {
                if (!IsStableExpression(operation.GetSource(index), depth + 1, intervalReads)) return false;
            }
            return true;
        }

        // A mutable local may be read without changing its value when no write
        // lies between the snapshot and its only consumer in the same block.
        // Check the actual enclosing statement, including nested expressions:
        // expression nodes need not have Parent/LLNode after prior propagation.
        // Calls and control flow are conservative boundaries; memory/derivative/
        // subgroup sources remain excluded by the pure-expression allowlist.
        private static bool CanMoveToSingleUse(AstAssignment assignment, AstOperand variable)
        {
            if (variable.Uses.Count != 1 || assignment.LLNode == null) return false;
            HashSet<AstOperand> reads = new();
            if (!IsStableExpression(assignment.Source, intervalReads: reads) || reads.Contains(variable)) return false;

            IAstNode use = variable.Uses.First();
            for (var next = assignment.LLNode.Next; next != null; next = next.Next)
            {
                IAstNode statement = next.Value;
                if (ClobbersRead(statement, reads)) return false;
                if (ContainsExpression(statement, use)) return true;
            }
            return false;
        }

        private static bool ClobbersRead(IAstNode node, HashSet<AstOperand> reads, int depth = 0)
        {
            if (depth > 128 || node is AstBlock) return true;
            if (node is AstAssignment assignment)
            {
                return (assignment.Destination is AstOperand destination && reads.Contains(destination)) ||
                    ClobbersRead(assignment.Source, reads, depth + 1);
            }
            if (node is AstOperation operation)
            {
                if ((operation.Inst & Instruction.Mask) == Instruction.Call ||
                    reads.Any(operand => operand.Defs.Contains(operation))) return true;
                for (int index = 0; index < operation.SourcesCount; index++)
                {
                    if (ClobbersRead(operation.GetSource(index), reads, depth + 1)) return true;
                }
                return false;
            }
            return node is not AstOperand;
        }

        private static bool ContainsExpression(IAstNode node, IAstNode expression, int depth = 0)
        {
            if (depth > 128) return false;
            if (node == expression) return true;
            if (node is AstAssignment assignment)
                return ContainsExpression(assignment.Source, expression, depth + 1);
            if (node is AstOperation operation)
            {
                for (int index = 0; index < operation.SourcesCount; index++)
                {
                    if (ContainsExpression(operation.GetSource(index), expression, depth + 1)) return true;
                }
            }
            return false;
        }

        private static bool IsWorthPropagating(IAstNode source)
        {
            if (source is not AstOperation srcOp)
            {
                return false;
            }

            if (!InstructionInfo.IsUnary(srcOp.Inst))
            {
                return false;
            }

            return srcOp.GetSource(0) is AstOperand || srcOp.Inst == Instruction.Copy;
        }

        private static void PropagateExpression(AstOperand propVar, IAstNode source)
        {
            IAstNode[] uses = propVar.Uses.ToArray();

            foreach (IAstNode useNode in uses)
            {
                if (useNode is AstBlock useBlock)
                {
                    useBlock.Condition = source;
                }
                else if (useNode is AstOperation useOperation)
                {
                    for (int srcIndex = 0; srcIndex < useOperation.SourcesCount; srcIndex++)
                    {
                        if (useOperation.GetSource(srcIndex) == propVar)
                        {
                            useOperation.SetSource(srcIndex, source);
                        }
                    }
                }
                else if (useNode is AstAssignment useAssignment)
                {
                    useAssignment.Source = source;
                }
            }
        }

        private static void RemoveEmptyBlocks(AstBlock mainBlock)
        {
            Queue<AstBlock> pending = new();

            pending.Enqueue(mainBlock);

            while (pending.TryDequeue(out AstBlock block))
            {
                foreach (IAstNode node in block)
                {
                    if (node is AstBlock childBlock)
                    {
                        pending.Enqueue(childBlock);
                    }
                }

                AstBlock parent = block.Parent;

                if (parent == null)
                {
                    continue;
                }

                AstBlock nextBlock = Next(block) as AstBlock;

                bool hasElse = nextBlock != null && nextBlock.Type == AstBlockType.Else;

                bool isIf = block.Type == AstBlockType.If;

                if (block.Count == 0)
                {
                    if (isIf)
                    {
                        if (hasElse)
                        {
                            nextBlock.TurnIntoIf(InverseCond(block.Condition));
                        }

                        parent.Remove(block);
                    }
                    else if (block.Type == AstBlockType.Else)
                    {
                        parent.Remove(block);
                    }
                }
                else if (isIf && parent.Type == AstBlockType.Else && parent.Count == (hasElse ? 2 : 1))
                {
                    AstBlock parentOfParent = parent.Parent;

                    parent.Remove(block);

                    parentOfParent.AddAfter(parent, block);

                    if (hasElse)
                    {
                        parent.Remove(nextBlock);

                        parentOfParent.AddAfter(block, nextBlock);
                    }

                    parentOfParent.Remove(parent);

                    block.TurnIntoElseIf();
                }
            }
        }
    }
}
