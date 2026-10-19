using Ryujinx.Graphics.Shader.IntermediateRepresentation;

namespace Ryujinx.Graphics.Shader.Translation
{
    readonly ref struct TransformContext
    {
        public readonly bool IsEntryFunction;
        public readonly bool QuadReadonlyModule;
        public readonly HelperFunctionManager Hfm;
        public readonly BasicBlock[] Blocks;
        public readonly ShaderDefinitions Definitions;
        public readonly ResourceManager ResourceManager;
        public readonly IGpuAccessor GpuAccessor;
        public readonly TargetApi TargetApi;
        public readonly TargetLanguage TargetLanguage;
        public readonly ShaderStage Stage;
        public readonly ref FeatureFlags UsedFeatures;

        public TransformContext(
            HelperFunctionManager hfm,
            BasicBlock[] blocks,
            ShaderDefinitions definitions,
            ResourceManager resourceManager,
            IGpuAccessor gpuAccessor,
            TargetApi targetApi,
            TargetLanguage targetLanguage,
            ShaderStage stage,
            ref FeatureFlags usedFeatures,
            bool isEntryFunction = false,
            bool quadReadonlyModule = false)
        {
            IsEntryFunction = isEntryFunction;
            QuadReadonlyModule = quadReadonlyModule;
            Hfm = hfm;
            Blocks = blocks;
            Definitions = definitions;
            ResourceManager = resourceManager;
            GpuAccessor = gpuAccessor;
            TargetApi = targetApi;
            TargetLanguage = targetLanguage;
            Stage = stage;
            UsedFeatures = ref usedFeatures;
        }
    }
}
