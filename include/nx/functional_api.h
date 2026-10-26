/* The contract between the client and its updatable functional part.
 *
 * A plain C ABI on purpose: the client and each version of the functional part
 * may be built at different times and with different compilers, so nothing that
 * depends on the C++ standard library crosses this boundary. NX_ABI_VERSION is
 * bumped whenever these signatures change; the client refuses a module whose ABI
 * it does not know, and keeps running the previous one. */
#ifndef NX_FUNCTIONAL_API_H
#define NX_FUNCTIONAL_API_H

#define NX_ABI_VERSION 1u

#if defined(_WIN32)
#  if defined(NX_BUILDING_FUNCTIONAL)
#    define NX_EXPORT __declspec(dllexport)
#  else
#    define NX_EXPORT __declspec(dllimport)
#  endif
#else
#  define NX_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ABI the module was built against (must equal NX_ABI_VERSION). */
NX_EXPORT unsigned nx_abi_version(void);
/* Semantic version of the module, e.g. "2.0.0". Static storage. */
NX_EXPORT const char* nx_version(void);
/* The functional part itself: prints its version. */
NX_EXPORT void nx_run(void);

#ifdef __cplusplus
}
#endif

/* Symbol names the loader resolves. */
#define NX_SYM_ABI "nx_abi_version"
#define NX_SYM_VERSION "nx_version"
#define NX_SYM_RUN "nx_run"

#endif
