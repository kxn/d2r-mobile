// 调用地址/输出及副作用的优化回归；不包含游戏或 CPU 模拟执行。
// Optimizer regression for call addresses, outputs and effects; no game/CPU emulation.
using System;
using Ryujinx.Graphics.Shader;
using Ryujinx.Graphics.Shader.IntermediateRepresentation;
using Ryujinx.Graphics.Shader.StructuredIr;
using Ryujinx.Graphics.Shader.Translation;

static class Program
{
    static void Check(bool value, string message)
    {
        if (!value) throw new Exception(message);
    }

    static StructuredProgramContext Context(bool debug = false)
    {
        var context = new StructuredProgramContext(default,
            new ShaderDefinitions(ShaderStage.Compute, 1, 1, 1), null, debug);
        context.EnterFunction(1, "test", AggregateType.Void, [], []);
        return context;
    }

    static AstOperand Const(int value) => new(OperandType.Constant, value);

    static void Main()
    {
        var input = Context();
        var argument = input.NewTemp(AggregateType.S32);
        var assignment = new AstAssignment(argument, Const(7));
        var call = new AstOperation(Instruction.Call, Const(1), argument);
        input.AddNode(assignment);
        input.AddNode(call);
        AstOptimizer.Optimize(input);
        Check(call.GetSource(1) == argument && input.CurrentFunction.Locals.Contains(argument)
              && assignment.Parent != null, "call input lost its Function storage/initialization");

        var output = Context();
        var outArg = output.NewTemp(AggregateType.S32);
        var initialize = new AstAssignment(outArg, Const(9));
        var outCall = new AstOperation(Instruction.Call, StorageKind.None, false,
            new IAstNode[] { Const(1), outArg }, 1);
        output.AddNode(initialize);
        output.AddNode(outCall);
        AstOptimizer.Optimize(output);
        Check(output.CurrentFunction.Locals.Contains(outArg) && outCall.GetSource(1) == outArg,
              "unused call output lost required Function storage");

        var effect = Context();
        var unused = effect.NewTemp(AggregateType.S32);
        var effectCall = new AstOperation(Instruction.Call, Const(1));
        var effectAssignment = new AstAssignment(unused, effectCall);
        effect.AddNode(effectAssignment);
        AstOptimizer.Optimize(effect);
        Check(effectAssignment.Parent != null && effectAssignment.Source == effectCall,
              "unused return discarded a call with possible side effects");

        // Reading a value and then calling through its address is a snapshot,
        // not permission to re-evaluate the expression after the call.
        var alias = Context();
        var mutable = alias.NewTemp(AggregateType.S32);
        var captured = alias.NewTemp(AggregateType.S32);
        var initMutable = new AstAssignment(mutable, Const(3));
        var expression = new AstOperation(Instruction.Multiply, mutable, Const(2));
        var snapshot = new AstAssignment(captured, expression);
        var mutateCall = new AstOperation(Instruction.Call, StorageKind.None, false,
            new IAstNode[] { Const(1), mutable }, 1);
        var returnSnapshot = new AstOperation(Instruction.Return, captured);
        alias.AddNode(initMutable); alias.AddNode(snapshot); alias.AddNode(mutateCall); alias.AddNode(returnSnapshot);
        AstOptimizer.Optimize(alias);
        Check(snapshot.Parent != null && returnSnapshot.GetSource(0) == captured,
              "expression moved its read of a call output across mutation");

        var repeated = Context();
        var phi = repeated.NewTemp(AggregateType.S32);
        var savedValue = repeated.NewTemp(AggregateType.S32);
        repeated.AddNode(new AstAssignment(phi, Const(1)));
        var beforeUpdate = new AstAssignment(savedValue, new AstOperation(Instruction.Add, phi, Const(1)));
        repeated.AddNode(beforeUpdate);
        repeated.AddNode(new AstAssignment(phi, Const(2)));
        var useBeforeUpdate = new AstOperation(Instruction.Return, savedValue);
        repeated.AddNode(useBeforeUpdate);
        AstOptimizer.Optimize(repeated);
        Check(beforeUpdate.Parent != null && useBeforeUpdate.GetSource(0) == savedValue,
              "propagation moved a read of a multiply-defined local");

        // Multiple definitions alone do not make a read volatile. Preserve its
        // value by proving the definition-to-use interval has no clobber.
        foreach (bool nested in new[] { false, true })
        {
            var interval = Context();
            var changedLater = interval.NewTemp(AggregateType.S32);
            var value = interval.NewTemp(AggregateType.S32);
            interval.AddNode(new AstAssignment(changedLater, Const(3)));
            var product = new AstOperation(Instruction.Multiply, changedLater, Const(2));
            var saved = new AstAssignment(value, product);
            interval.AddNode(saved);
            var consume = nested ? new AstOperation(Instruction.Negate, value) : null;
            var write = new AstOperation(Instruction.Store, Const(0), consume ?? (IAstNode)value);
            interval.AddNode(write);
            interval.AddNode(new AstAssignment(changedLater, Const(4)));
            AstOptimizer.Optimize(interval);
            Check(saved.Parent == null && (nested ? consume.GetSource(0) : write.GetSource(1)) == product,
                  "pure expression was not propagated across a clobber-free basic-block interval");
        }

        // A nested use must still remain a snapshot when its input changes.
        var nestedMutation = Context();
        var nestedInput = nestedMutation.NewTemp(AggregateType.S32);
        var nestedSaved = nestedMutation.NewTemp(AggregateType.S32);
        nestedMutation.AddNode(new AstAssignment(nestedInput, Const(3)));
        var nestedAssignment = new AstAssignment(nestedSaved, new AstOperation(Instruction.Add, nestedInput, Const(2)));
        nestedMutation.AddNode(nestedAssignment);
        nestedMutation.AddNode(new AstAssignment(nestedInput, Const(5)));
        var nestedUse = new AstOperation(Instruction.Negate, nestedSaved);
        nestedMutation.AddNode(new AstOperation(Instruction.Store, Const(0), nestedUse));
        AstOptimizer.Optimize(nestedMutation);
        Check(nestedAssignment.Parent != null && nestedUse.GetSource(0) == nestedSaved,
              "nested expression moved its snapshot past a write");

        // Even an unrelated call is a conservative boundary for interval proof.
        var callBoundary = Context();
        var callInput = callBoundary.NewTemp(AggregateType.S32);
        var callSaved = callBoundary.NewTemp(AggregateType.S32);
        callBoundary.AddNode(new AstAssignment(callInput, Const(1)));
        var callAssignment = new AstAssignment(callSaved, new AstOperation(Instruction.Add, callInput, Const(2)));
        callBoundary.AddNode(callAssignment);
        callBoundary.AddNode(new AstOperation(Instruction.Call, Const(1)));
        var callUse = new AstOperation(Instruction.Store, Const(0), callSaved);
        callBoundary.AddNode(callUse);
        callBoundary.AddNode(new AstAssignment(callInput, Const(4)));
        AstOptimizer.Optimize(callBoundary);
        Check(callAssignment.Parent != null && callUse.GetSource(1) == callSaved,
              "mutable expression crossed a call boundary");

        var controlBoundary = Context();
        var controlInput = controlBoundary.NewTemp(AggregateType.S32);
        var controlSaved = controlBoundary.NewTemp(AggregateType.S32);
        controlBoundary.AddNode(new AstAssignment(controlInput, Const(1)));
        var controlAssignment = new AstAssignment(controlSaved, new AstOperation(Instruction.Add, controlInput, Const(2)));
        controlBoundary.AddNode(controlAssignment);
        var conditional = new AstBlock(AstBlockType.If, Const(1));
        var controlUse = new AstOperation(Instruction.Store, Const(0), controlSaved);
        conditional.Add(controlUse);
        controlBoundary.AddNode(conditional);
        controlBoundary.AddNode(new AstAssignment(controlInput, Const(4)));
        AstOptimizer.Optimize(controlBoundary);
        Check(controlAssignment.Parent != null && controlUse.GetSource(1) == controlSaved,
              "mutable expression crossed a control-flow boundary");

        foreach (StorageKind kind in new[] { StorageKind.ConstantBuffer, StorageKind.StorageBuffer,
                                            StorageKind.SharedMemory, StorageKind.LocalMemory })
        {
            var memory = Context();
            var loaded = memory.NewTemp(AggregateType.S32);
            var load = new AstOperation(Instruction.Load, kind, false,
                new IAstNode[] { Const(0), Const(0), Const(0), Const(0) }, 4);
            var read = new AstAssignment(loaded, load);
            var useLoad = new AstOperation(Instruction.Return, loaded);
            memory.AddNode(read);
            memory.AddNode(useLoad);
            AstOptimizer.Optimize(memory);
            Check(read.Parent != null && useLoad.GetSource(0) == loaded,
                  "memory-load propagation enabled before whole-frame acceptance: " + kind);
        }

        // A uniform load may enter an immediately following pure expression,
        // but must retain its position when any statement intervenes.
        foreach (StorageKind kind in new[] { StorageKind.ConstantBuffer, StorageKind.StorageBuffer })
        foreach (bool intervening in new[] { false, true })
        foreach (bool duplicate in new[] { false, true })
        {
            var adjacent = Context();
            var loaded = adjacent.NewTemp(AggregateType.S32);
            var result = adjacent.NewTemp(AggregateType.S32);
            var load = new AstOperation(Instruction.Load, kind, false,
                new IAstNode[] { Const(0), Const(0), Const(0), Const(0) }, 4);
            var read = new AstAssignment(loaded, load);
            var product = new AstOperation(Instruction.Multiply, loaded, duplicate ? loaded : Const(2));
            adjacent.AddNode(read);
            if (intervening) adjacent.AddNode(new AstOperation(Instruction.Barrier));
            adjacent.AddNode(new AstAssignment(result, product));
            adjacent.AddNode(new AstOperation(Instruction.Return, result));
            AstOptimizer.Optimize(adjacent);
            bool folded = kind == StorageKind.ConstantBuffer && !intervening && !duplicate;
            Check(folded ? read.Parent == null && product.GetSource(0) == load
                         : read.Parent != null && product.GetSource(0) == loaded,
                  "adjacent uniform read proof failed: " + kind + " boundary=" + intervening);
        }

        var quad = Context();
        var derivative = quad.NewTemp(AggregateType.FP32);
        var ddx = new AstAssignment(derivative, new AstOperation(Instruction.Ddx, Const(0)));
        quad.AddNode(ddx);
        var branch = new AstBlock(AstBlockType.If, Const(1));
        branch.Add(new AstOperation(Instruction.Return, derivative));
        quad.AddNode(branch);
        AstOptimizer.Optimize(quad);
        Check(ddx.Parent != null, "derivative moved into conditional execution");

        foreach (bool debug in new[] { false, true })
        {
            var plain = Context(debug);
            var temporary = plain.NewTemp(AggregateType.S32);
            var copy = new AstAssignment(temporary, Const(5));
            var use = new AstOperation(Instruction.Return, temporary);
            plain.AddNode(copy);
            plain.AddNode(use);
            AstOptimizer.Optimize(plain);
            Check(debug ? copy.Parent != null && use.GetSource(0) == temporary
                        : copy.Parent == null && use.GetSource(0) is AstOperand { Value: 5 },
                  "ordinary propagation or debug reference changed");
        }
        Console.WriteLine("Shader AST regression passed: input/output addresses, call effects, ordinary propagation and debug reference");
    }
}
