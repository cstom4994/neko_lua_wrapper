#pragma once

#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "neko_lua_wrapper.hpp"

namespace neko {

namespace luabind {

// fwd
template <typename T>
struct LuaStack;

// lua2struct

namespace lua2struct {

using namespace neko;

template <typename T, typename I>
constexpr bool check_integral_limit(I i) {
    static_assert(std::is_integral_v<I>);
    static_assert(std::is_integral_v<T>);
    static_assert(sizeof(I) >= sizeof(T));
    if constexpr (sizeof(I) == sizeof(T)) {
        return true;
    } else if constexpr (std::numeric_limits<I>::is_signed == std::numeric_limits<T>::is_signed) {
        return i >= std::numeric_limits<T>::lowest() && i <= (std::numeric_limits<T>::max)();
    } else if constexpr (std::numeric_limits<I>::is_signed) {
        return static_cast<std::make_unsigned_t<I>>(i) >= std::numeric_limits<T>::lowest() && static_cast<std::make_unsigned_t<I>>(i) <= (std::numeric_limits<T>::max)();
    } else {
        return static_cast<std::make_signed_t<I>>(i) >= std::numeric_limits<T>::lowest() && static_cast<std::make_signed_t<I>>(i) <= (std::numeric_limits<T>::max)();
    }
}

template <typename T>
T unpack(lua_State *L, int arg);

template <typename T, std::size_t I>
void unpack_struct(lua_State *L, int arg, T &v);

template <typename T>
    requires(std::is_integral_v<T> && !std::same_as<T, bool>)
T unpack(lua_State *L, int arg) {
    lua_Integer r = luaL_checkinteger(L, arg);
    if constexpr (std::is_same_v<T, lua_Integer>) {
        return r;
    } else if constexpr (std::is_integral_v<T>) {
        return static_cast<T>(r);
    } else if constexpr (sizeof(T) >= sizeof(lua_Integer)) {
        return static_cast<T>(r);
    } else {
        if (check_integral_limit<T>(r)) {
            return static_cast<T>(r);
        }
        luaL_error(L, "unpack integer limit exceeded", arg);
    }
    assert(false);
    return T{};
}

template <typename T>
    requires(std::same_as<T, bool>)
T unpack(lua_State *L, int arg) {
    luaL_checktype(L, arg, LUA_TBOOLEAN);
    return !!lua_toboolean(L, arg);
}

template <typename T>
    requires std::is_pointer_v<T>
T unpack(lua_State *L, int arg) {
    luaL_checktype(L, arg, LUA_TLIGHTUSERDATA);
    return static_cast<T>(lua_touserdata(L, arg));
}

template <>
inline float unpack<float>(lua_State *L, int arg) {
    return (float)luaL_checknumber(L, arg);
}

template <>
inline std::string unpack<std::string>(lua_State *L, int arg) {
    size_t sz = 0;
    const char *str = luaL_checklstring(L, arg, &sz);
    return std::string(str, sz);
}

template <>
inline std::string_view unpack<std::string_view>(lua_State *L, int arg) {
    size_t sz = 0;
    const char *str = luaL_checklstring(L, arg, &sz);
    return std::string_view(str, sz);
}

template <typename T>
    requires std::is_aggregate_v<T>
T unpack(lua_State *L, int arg) {
    T v;
    unpack_struct<T, 0>(L, arg, v);
    return v;
}

template <template <typename...> class Template, typename Class>
struct is_instantiation : std::false_type {};
template <template <typename...> class Template, typename... Args>
struct is_instantiation<Template, Template<Args...>> : std::true_type {};
template <typename Class, template <typename...> class Template>
concept is_instantiation_of = is_instantiation<Template, Class>::value;

template <typename T>
    requires is_instantiation_of<T, std::map>
T unpack(lua_State *L, int arg) {
    arg = lua_absindex(L, arg);
    luaL_checktype(L, arg, LUA_TTABLE);
    T v;
    lua_pushnil(L);
    while (lua_next(L, arg)) {
        auto key = unpack<typename T::key_type>(L, -2);
        auto mapped = unpack<typename T::mapped_type>(L, -1);
        v.emplace(std::move(key), std::move(mapped));
        lua_pop(L, 1);
    }
    return v;
}

template <typename T>
    requires is_instantiation_of<T, std::vector>
T unpack(lua_State *L, int arg) {
    arg = lua_absindex(L, arg);
    luaL_checktype(L, arg, LUA_TTABLE);
    lua_Integer n = luaL_len(L, arg);
    T v;
    v.reserve((size_t)n);
    for (lua_Integer i = 1; i <= n; ++i) {
        lua_geti(L, arg, i);
        auto value = unpack<typename T::value_type>(L, -1);
        v.emplace_back(std::move(value));
        lua_pop(L, 1);
    }
    return v;
}

template <typename T, std::size_t I>
void unpack_struct(lua_State *L, int arg, T &v) {
    if constexpr (I < reflection::field_count<T>) {
        constexpr auto name = reflection::field_name<T, I>;
        lua_getfield(L, arg, name.data());
        reflection::field_access<I>(v) = unpack<reflection::field_type<T, I>>(L, -1);
        lua_pop(L, 1);
        unpack_struct<T, I + 1>(L, arg, v);
    }
}

template <typename T>
void pack(lua_State *L, const T &v) {
    LuaStack<T>::Push(L, v);
}

template <typename T, std::size_t I>
void pack_struct(lua_State *L, const T &v);

// template <typename T>
//     requires(std::is_integral_v<T> && !std::same_as<T, bool>)
// void pack(lua_State *L, const T &v) {
//     if constexpr (std::is_same_v<T, lua_Integer>) {
//         lua_pushinteger(L, v);
//     } else if constexpr (sizeof(T) <= sizeof(lua_Integer)) {
//         lua_pushinteger(L, static_cast<lua_Integer>(v));
//     } else {
//         if (check_integral_limit<lua_Integer>(v)) {
//             lua_pushinteger(L, static_cast<lua_Integer>(v));
//         }
//         luaL_error(L, "pack integer limit exceeded");
//         neko_assert(false);
//     }
// }

// template <typename T>
//     requires(std::same_as<T, bool>)
// void pack(lua_State *L, const T &v) {
//     lua_pushboolean(L, v);
// }

// template <>
// inline void pack<float>(lua_State *L, const float &v) {
//     lua_pushnumber(L, (lua_Number)v);
// }

// template <>
// inline void pack<int>(lua_State *L, const int &v) {
//     lua_pushnumber(L, (lua_Number)v);
// }

template <typename T>
    requires std::is_aggregate_v<T>
void pack(lua_State *L, const T &v) {
    lua_createtable(L, 0, (int)reflection::field_count<T>);
    pack_struct<T, 0>(L, v);
}

template <typename T, std::size_t I>
void pack_struct(lua_State *L, const T &v) {
    if constexpr (I < reflection::field_count<T>) {
        constexpr auto name = reflection::field_name<T, I>;
        pack<reflection::field_type<T, I>>(L, reflection::field_access<I>(v));
        lua_setfield(L, -2, name.data());
        pack_struct<T, I + 1>(L, v);
    }
}
}  // namespace lua2struct

}  // namespace luabind

}  // namespace neko