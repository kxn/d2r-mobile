using Ryujinx.Graphics.Shader.StructuredIr;
using Ryujinx.Graphics.Shader.Translation;
using Ryujinx.Graphics.Shader.IntermediateRepresentation;
using System;
using static Spv.Specification;
namespace Ryujinx.Graphics.Shader.CodeGen.Spirv
{
    using SpvInstruction = Spv.Generator.Instruction;
    static partial class Instructions
    {
        private static OperationResult GenerateQuadTransfer(CodeGenContext context, AstOperation operation)
        {
            if (operation.Index is not (1 or 2)) throw new InvalidOperationException("quad transfer axis");
            context.AddCapability(Capability.DerivativeControl);
            var f = context.TypeFP32(); var u = context.TypeU32(); var s = context.TypeS32();
            SpvInstruction Derivative(SpvInstruction x) => operation.Index == 1
                ? context.DPdxFine(f, x) : context.DPdyFine(f, x);
            SpvInstruction Add(SpvInstruction a, SpvInstruction b)
            {
                var r = context.FAdd(f, a, b); context.Decorate(r, Decoration.NoContraction); return r;
            }
            SpvInstruction Mul(SpvInstruction a, SpvInstruction b)
            {
                var r = context.FMul(f, a, b); context.Decorate(r, Decoration.NoContraction); return r;
            }
            // Signed floor is required for negative helper coordinates. The
            // profile requires framebuffer coordinates inside exact int32/f32
            // unit-resolution range, 1x1 fragment shading and sampleCount=1.
            var coord = context.GetFP32(operation.GetSource(1));
            var floored = context.GlslFloor(f, coord);
            var parityI = context.BitwiseAnd(s, context.ConvertFToS(s, floored), context.Constant(s, 1));
            var parity = context.ConvertSToF(f, parityI);
            var one = context.Constant(f, 1.0f);
            var two = context.Constant(f, 2.0f);
            var sign0 = Add(one, context.FNegate(f, Mul(two, parity)));
            var sign = Mul(sign0, Derivative(parity));
            var bits = context.Bitcast(u, context.GetFP32(operation.GetSource(0)));
            var mask = context.Constant(u, 65535u);
            var low = context.BitwiseAnd(u, bits, mask);
            var high = context.ShiftRightLogical(u, bits, context.Constant(u, 16u));
            SpvInstruction Transfer(SpvInstruction part)
            {
                var numeric = context.ConvertUToF(f, part);
                // Both uint16 inputs and their signed difference are exact
                // f32 integers; reconstruction is an exact value in0..65535.
                var other = Add(numeric, Mul(sign, Derivative(numeric)));
                return context.ConvertFToU(u, other);
            }
            var lo = Transfer(low); var hi = Transfer(high);
            var combined = context.BitwiseOr(u, lo, context.ShiftLeftLogical(u, hi, context.Constant(u, 16u)));
            return new OperationResult(AggregateType.FP32, context.Bitcast(f, combined));
        }
    }
}
