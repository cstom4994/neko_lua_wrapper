#pragma once

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <concepts>

typedef size_t usize;

typedef intptr_t iptr;
typedef uintptr_t uptr;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef float f32;
typedef double f64;
typedef const char *const_str;

struct String {
    char *data = nullptr;
    u64 len = 0;

    String() = default;
    String(const char *cstr) : data((char *)cstr), len(strlen(cstr)) {}
    String(const char *cstr, u64 n) : data((char *)cstr), len(n) {}

    inline bool is_cstr() { return data[len] == '\0'; }

    String substr(u64 i, u64 j) {
        assert(i <= j);
        assert(j <= (i64)len);
        return {&data[i], j - i};
    }

    inline bool operator==(String &rhs) {
        if (this->len != rhs.len) {
            return false;
        }
        return memcmp(this->data, rhs.data, this->len) == 0;
    }

    inline bool operator!=(String rhs) { return !(*this == rhs); }

    bool starts_with(String match) {
        if (len < match.len) {
            return false;
        }
        return substr(0, match.len) == match;
    }

    bool ends_with(String match) {
        if (len < match.len) {
            return false;
        }
        return substr(len - match.len, len) == match;
    }

    u64 first_of(char c) {
        for (u64 i = 0; i < len; i++) {
            if (data[i] == c) {
                return i;
            }
        }

        return (u64)-1;
    }

    u64 last_of(char c) {
        for (u64 i = len; i > 0; i--) {
            if (data[i - 1] == c) {
                return i - 1;
            }
        }

        return (u64)-1;
    }

    inline const_str cstr() const { return data; }

    char *begin() { return data; }
    char *end() { return &data[len]; }
};

String to_cstr(String str);

constexpr u64 fnv1a(const char *str, u64 len) {
    u64 hash = 14695981039346656037u;
    for (u64 i = 0; i < len; i++) {
        hash ^= (u8)str[i];
        hash *= 1099511628211;
    }
    return hash;
}

inline u64 fnv1a(String str) { return fnv1a(str.data, str.len); }

constexpr u64 operator"" _hash(const char *str, size_t len) { return fnv1a(str, len); }

inline String to_cstr(String str) {
    char *buf = (char *)malloc(str.len + 1);
    memcpy(buf, str.data, str.len);
    buf[str.len] = 0;
    return {buf, str.len};
}

template <typename T>
concept HasEqualOperator = requires(T a, T b) {
    { a == b } -> std::convertible_to<bool>;
};