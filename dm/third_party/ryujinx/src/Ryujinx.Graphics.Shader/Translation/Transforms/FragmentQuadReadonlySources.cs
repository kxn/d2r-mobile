using Ryujinx.Graphics.Shader.IntermediateRepresentation;
using Ryujinx.Graphics.Shader.StructuredIr;
using System;
using System.Collections.Generic;
using System.Linq;
using static Ryujinx.Graphics.Shader.IntermediateRepresentation.OperandHelper;
using static Ryujinx.Graphics.Shader.Translation.Translator;

namespace Ryujinx.Graphics.Shader.Translation.Transforms
{
    // Experimental V2 only. Additional readonly reads require a runtime contract
    // distinct from pure-input V1: robust buffers, stable legal descriptors and
    // no writable resource in any stage of the actual graphics draw.
    static class FragmentQuadReadonlySources
    {
        public static bool IsReadOnlyModule(FunctionCode[] functions)
        {
            foreach (var function in functions)
            foreach (var o in function.Code)
            {
                var i=o.Inst & Instruction.Mask;
                if (i==Instruction.Store && !o.StorageKind.IsOutput() && o.StorageKind!=StorageKind.LocalMemory)
                    return false;
                if (i.ToString().StartsWith("Atomic",StringComparison.Ordinal) ||
                    i is Instruction.ImageStore or Instruction.ImageAtomic or Instruction.Barrier or
                        Instruction.MemoryBarrier or Instruction.GroupMemoryBarrier or Instruction.FSIBegin or Instruction.FSIEnd)
                    return false;
                if (o is TextureOperation t && (t.Flags & TextureFlags.Coherent)!=0) return false;
            }
            return true;
        }

        [System.Diagnostics.CodeAnalysis.DoesNotReturn]
        private static void Fail(string why) => throw new InvalidOperationException("quad_transfer_readonly_v2: "+why);
        private static bool ConstValue(Operand v,int x) => v.Type==OperandType.Constant && v.Value==x;
        private static bool ScalarConstIndex(Operand v,int size) => v.Type==OperandType.Constant && (uint)v.Value<(uint)size;

        public static void Validate(TransformContext c, Operand root)
        {
            if(!c.QuadReadonlyModule) Fail("whole module has external writes, atomics or barriers");
            var seen=new HashSet<Operand>();var active=new HashSet<Operand>();
            Walk(c,root,seen,active,0);
        }

        private static void Walk(TransformContext c,Operand v,HashSet<Operand> seen,HashSet<Operand> active,int depth)
        {
            if(v.Type==OperandType.Constant) return;
            if(v.Type==OperandType.ConstantBuffer) {
                if((uint)v.GetCbufSlot()>=18 || (uint)v.GetCbufOffset()>=Constants.ConstantBufferSize/4) Fail("constant buffer literal exceeds declaration");
                return;
            }
            if(seen.Contains(v))return;
            if(depth>128 || seen.Count>2048 || !active.Add(v) || v.Type!=OperandType.LocalVariable || v.AsgOp is not Operation)
                Fail("unknown/cyclic/oversized source DAG");
            Operation o=(Operation)v.AsgOp; var i=o.Inst & Instruction.Mask;
            if(o is not TextureOperation && (o.DestsCount!=1 || o.Index!=0)) Fail("source must have one destination and default index");
            if(o is TextureOperation t) {
                if(i!=Instruction.TextureSample || t.Type!=SamplerType.Texture2D ||
                    t.Flags!=(TextureFlags.IntCoords|TextureFlags.LodLevel) || t.SamplerSet>=0 ||
                    t.SourcesCount!=3 || !ConstValue(t.GetSource(2),0) ||
                    !c.ResourceManager.Properties.Textures.TryGetValue(new SetBindingPair(t.Set,t.Binding),out var definition) ||
                    definition.Type!=SamplerType.Texture2D || definition.ArrayLength!=1 || definition.Separate ||
                    c.ResourceManager.IsArrayOfTexturesOrImages(t.Binding,false))
                    Fail("fetch must be nonarray plain2D explicit LOD0 with no offset/shadow/MS/bindless");
            } else if(i==Instruction.Load) {
                if(o.StorageKind==StorageKind.Input) {
                    bool coord=o.SourcesCount==2 && ConstValue(o.GetSource(0),(int)IoVariable.FragmentCoord) && ScalarConstIndex(o.GetSource(1),2);
                    bool user=o.SourcesCount==3 && ConstValue(o.GetSource(0),(int)IoVariable.UserDefined) && ScalarConstIndex(o.GetSource(1),256) && ScalarConstIndex(o.GetSource(2),4);
                    if(!coord&&!user)Fail("unsupported input source");
                } else if(o.StorageKind==StorageKind.ConstantBuffer) {
                    BufferDefinition b=default;
                    if(o.SourcesCount<2 || !ScalarConstIndex(o.GetSource(0),128) ||
                        !c.ResourceManager.Properties.ConstantBuffers.TryGetValue(o.GetSource(0).Value,out b) ||
                        !ScalarConstIndex(o.GetSource(1),b.Type.Fields.Length))Fail("unknown UBO declaration/member");
                    var field=b.Type.Fields[o.GetSource(1).Value]; int n=2;
                    if((field.Type&AggregateType.Array)!=0 && (n>=o.SourcesCount || !ScalarConstIndex(o.GetSource(n++),field.ArrayLength)))Fail("UBO array index not statically bounded");
                    int vec=(field.Type&AggregateType.ElementCountMask) switch {AggregateType.Vector2=>2,AggregateType.Vector3=>3,AggregateType.Vector4=>4,_=>1};
                    if(vec>1 && (n>=o.SourcesCount || !ScalarConstIndex(o.GetSource(n++),vec)))Fail("UBO vector index not bounded");
                    if(n!=o.SourcesCount)Fail("extra UBO indices");
                } else if(o.StorageKind==StorageKind.StorageBuffer) {
                    if(o.SourcesCount!=3 || !ScalarConstIndex(o.GetSource(0),128) || !ConstValue(o.GetSource(1),0) ||
                        !c.ResourceManager.Properties.StorageBuffers.TryGetValue(o.GetSource(0).Value,out var b) ||
                        b.Type.Fields.Length!=1 || b.Type.Fields[0].ArrayLength!=0 ||
                        b.Type.Fields[0].Type!=(AggregateType.Array|AggregateType.U32) ||
                        !c.ResourceManager.GetStorageBufferDescriptors().Any(x=>x.Binding==b.Binding) ||
                        c.ResourceManager.GetStorageBufferDescriptors().Any(x=>x.Binding==b.Binding && (x.Flags&BufferUsageFlags.Write)!=0))
                        Fail("storage source must be readonly runtimearray<uint32>; robust contract required");
                } else Fail("unsupported storage source");
            } else if(i==Instruction.Call) {
                if(o.DestsCount!=1 || o.SourcesCount!=4 || !ScalarConstIndex(o.GetSource(0),int.MaxValue) ||
                    !c.Hfm.IsGeneratedHelper(o.GetSource(0).Value,HelperFunctionName.TexelFetchScale) ||
                    !ScalarConstIndex(o.GetSource(2),SupportBuffer.RenderScaleMaxCount-1) || !ScalarConstIndex(o.GetSource(3),2))
                    Fail("only proven generated TexelFetchScale with constant bounded sampler/axis is supported");
            } else if(i is not (Instruction.Copy or Instruction.Add or Instruction.Negate or Instruction.Multiply or
                Instruction.Divide or Instruction.FusedMultiplyAdd or Instruction.Truncate or Instruction.Floor or
                Instruction.ConvertFP32ToS32 or Instruction.ConvertS32ToFP32 or Instruction.ConvertFP32ToU32 or
                Instruction.ConvertU32ToFP32 or Instruction.ShiftRightU32 or Instruction.ShiftRightS32 or Instruction.ShiftLeft or
                Instruction.BitwiseAnd or Instruction.BitwiseOr or Instruction.BitwiseExclusiveOr or Instruction.Minimum or Instruction.Maximum))
                Fail("unsupported source opcode "+o.Inst);
            if((o.Inst&~Instruction.Mask)!=0 && (o.Inst&~Instruction.Mask)!=Instruction.FP32)Fail("only 32bit source arithmetic supported");
            for(int k=0;k<o.SourcesCount;k++)Walk(c,o.GetSource(k),seen,active,depth+1);
            active.Remove(v);seen.Add(v);
        }

        public static Operand Clone(TransformContext c,Operand v,Dictionary<Operand,Operand> cache,List<Operation> prefix)
        {
            if(v.Type==OperandType.ConstantBuffer) {c.UsedFeatures|=FeatureFlags.FragmentQuadReadonlySources;return v;}
            if(v.Type==OperandType.Constant)return v;
            if(cache.TryGetValue(v,out var found))return found;
            Operation o=(Operation)v.AsgOp;
            Operand[] src=new Operand[o.SourcesCount];
            for(int k=0;k<src.Length;k++)src[k]=Clone(c,o.GetSource(k),cache,prefix);
            var dst=Enumerable.Range(0,o.DestsCount).Select(_=>Local()).ToArray();
            if(o is TextureOperation t) {
                Operand[] dim=new Operand[2];Operand valid=Const(IrConsts.True);Operand[] safe=new Operand[3];safe[2]=Const(0);
                Operand Emit(Instruction inst,params Operand[] args) {var x=Local();prefix.Add(new Operation(inst,x,args));return x;}
                for(int a=0;a<2;a++) {
                    dim[a]=Local();prefix.Add(new TextureOperation(Instruction.TextureQuerySize,t.Type,t.Format,TextureFlags.None,t.Set,t.Binding,a,[dim[a]],[Const(0)]));
                    var positive=Emit(Instruction.CompareGreater,dim[a],Const(0));
                    var nonnegative=Emit(Instruction.CompareGreaterOrEqual,src[a],Const(0));
                    var below=Emit(Instruction.CompareLess,src[a],dim[a]);
                    valid=Emit(Instruction.LogicalAnd,valid,Emit(Instruction.LogicalAnd,positive,Emit(Instruction.LogicalAnd,nonnegative,below)));
                    var last=Emit(Instruction.Maximum,Emit(Instruction.Add,dim[a],Const(-1)),Const(0));
                    safe[a]=Emit(Instruction.Minimum,Emit(Instruction.Maximum,src[a],Const(0)),last);
                }
                c.ResourceManager.SetUsageFlagsForTextureQuery(t.Binding,t.Type);
                prefix.Add(new TextureOperation(t.Inst,t.Type,t.Format,t.Flags,t.Set,t.Binding,t.Index,dst,safe){ForcePrecise=t.ForcePrecise});
                for(int k=0;k<o.DestsCount;k++) {
                    var result=Emit(Instruction.ConditionalSelect,valid,dst[k],Const(0));cache.Add(o.GetDest(k),result);
                }
            } else {
                prefix.Add(new Operation(o.Inst,o.StorageKind,dst[0],src){ForcePrecise=o.ForcePrecise});
                cache.Add(v,dst[0]);
            }
            if(o.StorageKind is StorageKind.ConstantBuffer or StorageKind.StorageBuffer || v.Type==OperandType.ConstantBuffer || o is TextureOperation || (o.Inst&Instruction.Mask)==Instruction.Call)
                c.UsedFeatures|=FeatureFlags.FragmentQuadReadonlySources;
            return cache[v];
        }
    }
}
