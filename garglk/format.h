#ifndef GARGLK_FORMAT_H
#define GARGLK_FORMAT_H

#include <filesystem>
#include <string>

#define FMT_ENFORCE_COMPILE_STRING
#ifdef GARGLK_CONFIG_BUNDLED_FMT
#define FMT_HEADER_ONLY
#include "format/format.h"
#else
#include <fmt/format.h>
#endif

// This was added in 5.0.0.
#ifndef FMT_STRING
#define FMT_STRING(s) (s)
#endif

#define Format(s, ...) fmt::format(FMT_STRING(s), __VA_ARGS__)

// fmt can format std::filesystem::path, but only if fmt/std.h is
// included, which the bundled copy of fmt doesn't ship: it would mean
// vendoring both std.h and ostream.h, which between them pull in
// <thread>, <typeinfo> and <complex>, all for the sake of one type.
// This produces the same output fmt does, namely the path unquoted.
//
// If a future version of fmt formats paths without std.h, this will
// fail to compile as a duplicate specialization, at which point it can
// simply be deleted.
//
// format() has to be const, because fmt 8 and later call it on a const
// formatter; but fmt 7 and earlier declare their own format() non-const,
// so it can't be called on this one directly. Formatting a copy of the
// base satisfies both: the copy carries the spec parse() produced, and
// being non-const it can be used with any version.
template <>
struct fmt::formatter<std::filesystem::path> : fmt::formatter<std::string> {
    template <typename FormatContext>
    auto format(const std::filesystem::path &path, FormatContext &ctx) const {
        fmt::formatter<std::string> formatter = *this;
        return formatter.format(path.string(), ctx);
    }
};

#endif
