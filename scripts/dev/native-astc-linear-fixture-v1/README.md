# Fully-linear native ASTC: GPU filter reference fixture

This isolated successor does not replace the accepted original 32-shape-format
NEAREST matrix. Its independent ordinary-UID package is
`com.d2rmobile.nativeastcfilterfixture`, without game classes, assets, or NSOs.
It only loads hash-pinned f830 Turnip through the existing owned loader.

Four finite shapes: 1x1/1 mip, 5x7/3, 63x65/7, 128x256/9.
Each executes ASTC4/6 UNORM and sRGB, original two pixel patterns and two import
lifetimes. Existing host-image-copy producer generates the opaque bytes;
original hash/guards/all-mip transfer and NEAREST pixel checks remain.

Before any Buffer-to-Image upload can touch a newly imported native image,
run both original NEAREST checks and the added filter reference. Create an
ordinary private optimal ASTC image (`pNext=NULL`, independent VkDeviceMemory),
fill every mip from original compressed vectors using standard host image copy,
and transition it to sampling. Both image descriptors use the *same sampler*.
A minimal dual-source extension of original `sample.comp` performs two hardware
`textureLod` operations at identical UV/LOD and writes both RGBA float-bit
vectors. CPU only compares the resulting bits, dispatch markers, and unused
output guards; it does not decode ASTC or implement filtering.

For every baseMip and every remaining mip, bilinear uses LINEAR min/mag with
NEAREST mip mode and integer LOD; trilinear uses LINEAR mip mode and explicit
relative LOD +0.375. Fractional UV phases 0.375/0.625 cover every mip texel plus
an edge border through CLAMP_TO_EDGE. Tail views naturally clamp LOD at their
last mip. The same reference check is repeated after the existing native
B2T/T2B round trip, preserving pre-upload proof rather than allowing upload to
repair bad producer bytes. Full compared pixel and dispatch counts are checked.

Unsupported linear-filter format features are emitted explicitly and fail
closed; they are never reported as accepted filtering. No VVL layer is loaded;
this is functional GPU-reference evidence, not VUID coverage, performance,
bandwidth, thermal, real-game, or every-driver correctness evidence.

Build offline with `build.py --output NEW_DIRECTORY` (NDK parallelism 2).
The controller defaults offline. `--execute` requires a root-reviewed new
output, stopped original game, exact original baseline, ordinary first PID,
private mapped driver device/inode/bytes, and owned package installation.
It stops the fixture before a bounded archive transfer, uninstalls only that
package, cleans only its unique temporary directory, and rechecks the original
baseline. No system settings or original game data are written. A 240-second
functional bound is not a performance timer. No automatic retry.
