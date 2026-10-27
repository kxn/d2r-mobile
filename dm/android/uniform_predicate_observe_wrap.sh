#!/system/bin/sh
# Count only ready Uniform inputs using authenticated existing DPR recipes.
# Original shader modules, specialization and PSO selection remain in use.
# Diagnostic workload only; never use it as a normal performance baseline.
export DM_VULKAN_UNIFORM_PREDICATE_OBSERVE=1
exec "$@"
