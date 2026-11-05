// Firmware is built with -fno-exceptions. libstdc++'s std::__throw_* helpers would pull in the whole exception
// runtime, the verbose terminate handler, the C++ demangler and std::string (about 60 KB of flash).
// Defining them here keeps those archive members out of the image; a throw can never succeed on this target
// (it would end in std::terminate -> abort), so stopping here is the same observable behaviour.
#ifndef PLATFORM_IMX

#include <cstdlib>
#include <malloc.h>
#include <new>

namespace std
{
[[noreturn]] void __throw_bad_alloc() { abort(); }
[[noreturn]] void __throw_bad_cast() { abort(); }
[[noreturn]] void __throw_bad_exception() { abort(); }
[[noreturn]] void __throw_bad_typeid() { abort(); }
[[noreturn]] void __throw_bad_function_call() { abort(); }
[[noreturn]] void __throw_logic_error(const char*) { abort(); }
[[noreturn]] void __throw_domain_error(const char*) { abort(); }
[[noreturn]] void __throw_invalid_argument(const char*) { abort(); }
[[noreturn]] void __throw_length_error(const char*) { abort(); }
[[noreturn]] void __throw_out_of_range(const char*) { abort(); }
[[noreturn]] void __throw_out_of_range_fmt(const char*, ...) { abort(); }
[[noreturn]] void __throw_runtime_error(const char*) { abort(); }
[[noreturn]] void __throw_range_error(const char*) { abort(); }
[[noreturn]] void __throw_overflow_error(const char*) { abort(); }
[[noreturn]] void __throw_underflow_error(const char*) { abort(); }
} // namespace std

// DisplayDriverImpl (DMASetting is over-aligned) uses the aligned new[]; libstdc++'s version throws via __cxa_throw
// (the whole exception runtime). Same contract as the teensy core's plain operator new: malloc-style, null on failure.
void* operator new[](std::size_t size, std::align_val_t al)
{
    return memalign(static_cast<std::size_t>(al), size);
}

void* operator new(std::size_t size, std::align_val_t al)
{
    return memalign(static_cast<std::size_t>(al), size);
}

#endif
