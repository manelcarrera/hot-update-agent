// The updatable functional part. The same source is built once per version
// (NX_FUNCTIONAL_VERSION comes from CMake), producing functional_v1 and functional_v2.
#include "nx/functional_api.h"

#include <cstdio>

#ifndef NX_FUNCTIONAL_VERSION
#  error "NX_FUNCTIONAL_VERSION must be defined by the build"
#endif

extern "C" {

NX_EXPORT unsigned nx_abi_version(void) { return NX_ABI_VERSION; }

NX_EXPORT const char* nx_version(void) { return NX_FUNCTIONAL_VERSION; }

NX_EXPORT void nx_run(void) {
    std::printf("[functional] version %s\n", NX_FUNCTIONAL_VERSION);
    std::fflush(stdout);
}

}
