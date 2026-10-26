#include "nx/client.hpp"

#include "nx/functional_api.h"

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

#include <system_error>
#include <utility>

namespace nx {
namespace {

// --- the only platform-specific part of the host -------------------------------
void* lib_open(const std::filesystem::path& p, std::string& err) {
#if defined(_WIN32)
    HMODULE h = ::LoadLibraryW(p.wstring().c_str());
    if (!h) err = "LoadLibrary failed, error " + std::to_string(::GetLastError());
    return reinterpret_cast<void*>(h);
#else
    void* h = ::dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) err = ::dlerror();
    return h;
#endif
}

void* lib_symbol(void* h, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(::GetProcAddress(reinterpret_cast<HMODULE>(h), name));
#else
    return ::dlsym(h, name);
#endif
}

void lib_close(void* h) {
#if defined(_WIN32)
    ::FreeLibrary(reinterpret_cast<HMODULE>(h));
#else
    ::dlclose(h);
#endif
}

template <class Fn> Fn as_function(void* p) { return reinterpret_cast<Fn>(reinterpret_cast<std::uintptr_t>(p)); }

}  // namespace

Module::~Module() {
    if (handle_) lib_close(handle_);
    std::error_code ec;
    std::filesystem::remove(loaded_copy_, ec);  // best effort; Windows releases it after FreeLibrary
}

void Module::run() const { run_(); }

ModuleHost::ModuleHost(std::filesystem::path runtime_dir) : runtime_dir_(std::move(runtime_dir)) {
    std::error_code ec;
    std::filesystem::create_directories(runtime_dir_, ec);
}

bool ModuleHost::load(const std::filesystem::path& library, std::string* error) {
    auto fail = [&](std::string why) {
        log("host", "rejected " + library.string() + ": " + why);
        if (error) *error = std::move(why);
        return false;
    };

    // Private copy with a unique name (see the class comment).
    const auto copy = runtime_dir_ / ("functional-" + std::to_string(counter_++) + std::string(shared_lib_extension()));
    std::error_code ec;
    std::filesystem::copy_file(library, copy, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) return fail("cannot copy: " + ec.message());

    std::shared_ptr<Module> m(new Module());
    m->loaded_copy_ = copy;
    std::string err;
    m->handle_ = lib_open(copy, err);
    if (!m->handle_) return fail("cannot load: " + err);

    auto abi = as_function<unsigned (*)()>(lib_symbol(m->handle_, NX_SYM_ABI));
    auto ver = as_function<const char* (*)()>(lib_symbol(m->handle_, NX_SYM_VERSION));
    m->run_ = as_function<void (*)()>(lib_symbol(m->handle_, NX_SYM_RUN));
    if (!abi || !ver || !m->run_) return fail("missing symbols");
    if (abi() != NX_ABI_VERSION) return fail("ABI " + std::to_string(abi()) + " is not " + std::to_string(NX_ABI_VERSION));
    const char* v = ver();
    if (!v || !Version::parse(v)) return fail("invalid version string");
    m->version_ = v;

    std::shared_ptr<const Module> previous;
    {
        const std::lock_guard lock(mutex_);
        previous = std::exchange(current_, std::move(m));
    }
    log("host", "now running " + current()->version() + (previous ? " (was " + previous->version() + ")" : ""));
    current()->run();  // the functional part prints its version after every load
    return true;       // `previous` is unloaded here, or later if someone still uses it
}

std::shared_ptr<const Module> ModuleHost::current() const {
    const std::lock_guard lock(mutex_);
    return current_;
}

std::string ModuleHost::current_version() const {
    auto m = current();
    return m ? m->version() : std::string{};
}

}  // namespace nx
