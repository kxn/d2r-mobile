// Run the same embedded GPU oracles as the APK with all test-only hooks.
// A separate entry point keeps the production Android library entry unchanged.
extern int dm_vulkan_test_main(void);
int main(void) { return dm_vulkan_test_main(); }
