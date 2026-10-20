# One real Turnip native texture: isolated functional experiment

The current Q game remains original BC/UI4 with shader chain9507875007c6. This
fixture does not launch it or deploy any resources. The selected real firepit
albedo is256x256 with9mips. Original game format62/NVN0x48 is BC3sRGB; the
existing reproducible ASTC4 control is game format81/NVN0x87/VkFormat158.
An ASTC4 native full texture must retain its matching ASTC4 thumbnail/tail; BC3
thumbnail bytes cannot be interpreted as ASTC4. No whole Mali cache is imported.

The producer app uses the existing resource_bake entry under ordinary UID and
actualaa185 driver. It exports the existing DMNRES1 record, complete opaque
payload and queried layout/device/driver UUIDs. It does not implement tiling or
claim GPU sampling from host-copy alone. The original prepared real-resource
DMRSRC1 input and source declaration are hash-bound, not synthetic colors.

The host bundle script invokes existing pack-native-texture --driver-query,
using actual native-resource.json and the exact driver build record. It produces
one DMNTBL2 entry, not a new protocol. It rejects incomplete/failed producer
reports and hashes. Its output stays nondeployable game data. Only the two
existing owned control3/4 vertex/fragment shader modules are copied; they are
functional test shaders, not the current game's950chain or a Mali gamechain.

The family app is based on frozenfbd09b2 original NVN native-family oracle, with
only explicitTurnip fixture routing/authentication, cold TU_DM_HOST_IMPORT and
the already-tested MAP_SHARED startup-probe helper. All original NVN Builder,
StorageSize/Alignment,9mip blocks/samples,5tailmips, B2T/T2T/Buffer alias/CPU Flush,
neighbor and physical retirement assertions remain unchanged. CoherentTurnip
metadata uses cached+coherent properties; the C oracle's original coherent OR
prewrite-gate guard is retained. No unknown-writer path is waived.

This tests the existing native-family protocol in a finite4MiB owned Pool. It
is not integration into current2f's fullgame allocator/alias-lifetime branch.
Before future game adoption that composition and actual game's Builder/storage/
streaming/alias usage must be independently accepted. No driver defaults, game
binary, gameplay, realgame resources, perdraw counters or performance timers
are changed. Direct custom-ICD test validation is explicitly disabled: absence
of VUID text is not VVL/SYNC coverage. No RSS/DRAM/FPS benefit is claimed.

Build each private worktree with canonical scripts, committed source, TLSdesc,
O3 and native-jobs2. Producer additionally sets DM_ANDROID_NATIVE_IMAGE_FIXTURE;
family sets --shared-pools --native-resources. A family APK can be compiled
before the actual resource bake, but cannot run until a verified actual queried
resource bundle is bound using build-android-native-family-fixture --inputs.
Q lease must be coordinated with root/turnip_host_import. Neither controller
starts the original game, and only its own independent package may be stopped
and removed. Original APK/ELF/28module/950hashes are checked before and after.
