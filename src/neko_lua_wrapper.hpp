

#if !defined(NEKO_LUAX_HPP)
#define NEKO_LUAX_HPP

#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <format>
#include <initializer_list>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <string>
#include <tuple>  // std::ignore
#include <typeindex>
#include <utility>
#include <variant>
#include <vector>

#include "neko_lua_wrapper.h"
#include "pp.inl"

#define INHERIT_TABLE "inherit_table"

namespace neko {

namespace reflection {

template <unsigned short N>
struct cstring {
    constexpr explicit cstring(std::string_view str) noexcept : cstring{str, std::make_integer_sequence<unsigned short, N>{}} {}
    constexpr const char *data() const noexcept { return chars_; }
    constexpr unsigned short size() const noexcept { return N; }
    constexpr operator std::string_view() const noexcept { return {data(), size()}; }
    template <unsigned short... I>
    constexpr cstring(std::string_view str, std::integer_sequence<unsigned short, I...>) noexcept : chars_{str[I]..., '\0'} {}
    char chars_[static_cast<size_t>(N) + 1];
};
template <>
struct cstring<0> {
    constexpr explicit cstring(std::string_view) noexcept {}
    constexpr const char *data() const noexcept { return nullptr; }
    constexpr unsigned short size() const noexcept { return 0; }
    constexpr operator std::string_view() const noexcept { return {}; }
};

template <typename T>
constexpr auto name_raw() noexcept {
#if defined(__clang__) || defined(__GNUC__)
    std::string_view name = __PRETTY_FUNCTION__;
    size_t start = name.find('=') + 2;
    size_t end = name.size() - 1;
    return std::string_view{name.data() + start, end - start};
#elif defined(_MSC_VER)
    std::string_view name = __FUNCSIG__;
    size_t start = name.find('<') + 1;
    size_t end = name.rfind(">(");
    name = std::string_view{name.data() + start, end - start};
    start = name.find(' ');
    return start == std::string_view::npos ? name : std::string_view{name.data() + start + 1, name.size() - start - 1};
#else
#error Unsupported compiler
#endif
}

template <typename T>
constexpr auto name() noexcept {
    constexpr auto name = name_raw<T>();
    return cstring<name.size()>{name};
}
template <typename T>
constexpr auto name_v = name<T>();

struct DummyFlag {};
template <typename Enum, typename T, Enum enumValue>
inline int get_enum_value(std::map<int, std::string> &values) {
#if defined _MSC_VER && !defined __clang__
    std::string func(__FUNCSIG__);
    std::string mark = "DummyFlag";
    auto pos = func.find(mark) + mark.size();
    std::string enumStr = func.substr(pos);

    auto start = enumStr.find_first_not_of(", ");
    auto end = enumStr.find('>');
    if (start != enumStr.npos && end != enumStr.npos && enumStr[start] != '(') {
        enumStr = enumStr.substr(start, end - start);
        values.insert({(int)enumValue, enumStr});
    }

#else  // gcc, clang
    std::string func(__PRETTY_FUNCTION__);
    std::string mark = "enumValue = ";
    auto pos = func.find(mark) + mark.size();
    std::string enumStr = func.substr(pos, func.size() - pos - 1);
    char ch = enumStr[0];
    if (!(ch >= '0' && ch <= '9') && ch != '(') values.insert({(int)enumValue, enumStr});
#endif
    return 0;
}

template <typename Enum, int min_value, int... ints>
void guess_enum_range(std::map<int, std::string> &values, const std::integer_sequence<int, ints...> &) {
    auto dummy = {get_enum_value<Enum, DummyFlag, (Enum)(ints + min_value)>(values)...};
}

template <typename Enum, int... ints>
void guess_enum_bit_range(std::map<int, std::string> &values, const std::integer_sequence<int, ints...> &) {
    auto dummy = {get_enum_value<Enum, DummyFlag, (Enum)0>(values), get_enum_value<Enum, DummyFlag, (Enum)(1 << (int)ints)>(values)...};
}

template <typename T>
auto GetTypeName() {
    return reflection::name_v<T>.data();
}

template <typename T>
struct wrapper {
    T a;
};
template <typename T>
wrapper(T) -> wrapper<T>;

template <typename T>
static inline T storage = {};

template <auto T>
constexpr auto main_name_of_pointer() {
    constexpr auto is_identifier = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; };
#if __GNUC__ && (!__clang__) && (!_MSC_VER)
    std::string_view str = __PRETTY_FUNCTION__;
    std::size_t start = str.rfind("::") + 2;
    std::size_t end = start;
    for (; end < str.size() && is_identifier(str[end]); end++) {
    }
    return str.substr(start, end - start);
#elif __clang__
    std::string_view str = __PRETTY_FUNCTION__;
    std::size_t start = str.rfind(".") + 1;
    std::size_t end = start;
    for (; end < str.size() && is_identifier(str[end]); end++) {
    }
    return str.substr(start, end - start);
#elif _MSC_VER
    std::string_view str = __FUNCSIG__;
    std::size_t start = str.rfind("->") + 2;
    std::size_t end = start;
    for (; end < str.size() && is_identifier(str[end]); end++) {
    }
    return str.substr(start, end - start);
#else
    static_assert(false, "Not supported compiler");
#endif
}

// 无定义 我们需要一个可以转换为任何类型的在以下特殊语境中使用的辅助类
struct __Any {
    constexpr __Any(int) {}
    template <typename T>
        requires std::is_copy_constructible_v<T>
    constexpr operator T &() const;
    template <typename T>
        requires std::is_move_constructible_v<T>
    constexpr operator T &&() const;
    template <typename T>
        requires(!std::is_copy_constructible_v<T> && !std::is_move_constructible_v<T>)
    constexpr operator T() const;
};

// 计算结构体成员数量
#if !defined(_MSC_VER) || 0  // 我不知道为什么 if constexpr (!requires { T{Args...}; }) {...} 方法会导致目前版本的vs代码感知非常卡

template <typename T>
consteval size_t struct_size(auto &&...Args) {
    if constexpr (!requires { T{Args...}; }) {
        return sizeof...(Args) - 1;
    } else {
        return struct_size<T>(Args..., __any{});
    }
}

template <class T>
struct struct_member_count : std::integral_constant<std::size_t, struct_size<T>()> {};

#else

template <typename T, typename = void, typename... Ts>
struct struct_size {
    constexpr static size_t value = sizeof...(Ts) - 1;
};

template <typename T, typename... Ts>
struct struct_size<T, std::void_t<decltype(T{Ts{}...})>, Ts...> {
    constexpr static size_t value = struct_size<T, void, Ts..., __Any>::value;
};

template <class T>
struct struct_member_count : std::integral_constant<std::size_t, struct_size<T>::value> {};

#endif

template <typename T, std::size_t N>
constexpr std::size_t try_initialize_with_n() {
    return []<std::size_t... Is>(std::index_sequence<Is...>) { return requires { T{__Any(Is)...}; }; }(std::make_index_sequence<N>{});
}

template <typename T, std::size_t N = 0>
constexpr auto field_count_impl() {
    if constexpr (try_initialize_with_n<T, N>() && !try_initialize_with_n<T, N + 1>()) {
        return N;
    } else {
        return field_count_impl<T, N + 1>();
    }
}

template <typename T>
    requires std::is_aggregate_v<T>
static constexpr auto field_count = field_count_impl<T>();

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4101)
#endif

#define STRUCT_FIELD_TYPE_DEF(N)                                                                               \
    template <typename T, std::size_t I>                                                                       \
    constexpr auto __struct_field_type_impl(T my_struct, std::integral_constant<std::size_t, N>) {             \
        auto [NEKO_PP_PARAMS(x, N)] = my_struct;                                                               \
        return std::type_identity<std::tuple_element_t<I, std::tuple<NEKO_PP_CALL_PARAMS(decltype, x, N)>>>{}; \
    }

NEKO_PP_FOR_EACH(STRUCT_FIELD_TYPE_DEF, 63)

template <typename T, std::size_t I>
constexpr auto field_type_impl(T object) {
    constexpr auto N = field_count<T>;
    return __struct_field_type_impl<T, I>(object, std::integral_constant<std::size_t, N>{});
}

#define STRUCT_FIELD_ACCESS_DEF(N)                                                                          \
    template <std::size_t I>                                                                                \
    constexpr auto &&__struct_field_access_impl(auto &&my_struct, std::integral_constant<std::size_t, N>) { \
        auto &&[NEKO_PP_PARAMS(x, N)] = std::forward<decltype(my_struct)>(my_struct);                       \
        return std::get<I>(std::forward_as_tuple(NEKO_PP_PARAMS(x, N)));                                    \
    }

NEKO_PP_FOR_EACH(STRUCT_FIELD_ACCESS_DEF, 63)

template <std::size_t I>
constexpr auto &&field_access(auto &&object) {
    using T = std::remove_cvref_t<decltype(object)>;
    constexpr auto N = field_count<T>;
    return __struct_field_access_impl<I>(object, std::integral_constant<std::size_t, N>{});
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

template <typename T, std::size_t I>
constexpr auto field_name_impl() noexcept {
    constexpr auto name = main_name_of_pointer<wrapper{&field_access<I>(storage<T>)}>();
    return cstring<name.size()>{name};
}

template <typename T, std::size_t I>
    requires std::is_aggregate_v<T>
using field_type = typename decltype(field_type_impl<T, I>(std::declval<T>()))::type;

template <typename T, std::size_t I>
    requires std::is_aggregate_v<T>
static constexpr auto field_name = field_name_impl<T, I>();

}  // namespace reflection

namespace luabind {

#define PRELOAD(name, function)     \
    lua_getglobal(L, "package");    \
    lua_getfield(L, -1, "preload"); \
    lua_pushcfunction(L, function); \
    lua_setfield(L, -2, name);      \
    lua_pop(L, 2)

void luax_run_bootstrap(lua_State *L);
void luax_run_nekogame(lua_State *L);

i32 luax_require_script(lua_State *L, String filepath);

void luax_stack_dump(lua_State *L);

void luax_get(lua_State *L, const_str tb, const_str field);
void luax_pcall(lua_State *L, i32 args, i32 results);

void luax_neko_get(lua_State *L, const char *field);

int luax_msgh(lua_State *L);

lua_Integer luax_len(lua_State *L, i32 arg);
void luax_geti(lua_State *L, i32 arg, lua_Integer n);

// 将表值设置在堆栈顶部
void luax_set_number_field(lua_State *L, const char *key, lua_Number n);
void luax_set_int_field(lua_State *L, const char *key, lua_Integer n);
void luax_set_string_field(lua_State *L, const char *key, const char *str);

// 从表中获取值
lua_Number luax_number_field(lua_State *L, i32 arg, const char *key);
lua_Number luax_opt_number_field(lua_State *L, i32 arg, const char *key, lua_Number fallback);

lua_Integer luax_int_field(lua_State *L, i32 arg, const char *key);
lua_Integer luax_opt_int_field(lua_State *L, i32 arg, const char *key, lua_Integer fallback);

String luax_string_field(lua_State *L, i32 arg, const char *key);
String luax_opt_string_field(lua_State *L, i32 arg, const char *key, const char *fallback);

bool luax_boolean_field(lua_State *L, i32 arg, const char *key, bool fallback = false);

String luax_check_string(lua_State *L, i32 arg);
String luax_opt_string(lua_State *L, i32 arg, String def);

inline void luax_new_class(lua_State *L, const char *mt_name, const luaL_Reg *l) {
    luaL_newmetatable(L, mt_name);
    luaL_setfuncs(L, l, 0);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
}

enum {
    LUAX_UD_TNAME = 1,
    LUAX_UD_PTR_SIZE = 2,
};

#if !defined(NEKO_LUAJIT)
template <typename T>
void luax_new_userdata(lua_State *L, T data, const char *tname) {
    void *new_udata = lua_newuserdatauv(L, sizeof(T), 2);

    lua_pushstring(L, tname);
    lua_setiuservalue(L, -2, LUAX_UD_TNAME);

    lua_pushnumber(L, sizeof(T));
    lua_setiuservalue(L, -2, LUAX_UD_PTR_SIZE);

    memcpy(new_udata, &data, sizeof(T));
    luaL_setmetatable(L, tname);
}
#else
template <typename T>
void luax_new_userdata(lua_State *L, T data, const char *tname) {
    T *new_udata = (T *)lua_newuserdata(L, sizeof(T));

    // 为用户数据设置元表
    luaL_getmetatable(L, tname);
    lua_setmetatable(L, -2);

    memcpy(new_udata, &data, sizeof(T));

    // 额外的值使用lua_setfield将其存储在表中
    lua_newtable(L);

    lua_pushstring(L, tname);
    lua_setfield(L, -2, "tname");

    lua_pushnumber(L, sizeof(T));
    lua_setfield(L, -2, "size");

    // 将这个表作为用户数据的环境表存储
    lua_setfenv(L, -2);
}
#endif

#define luax_ptr_userdata luax_new_userdata

int __neko_bind_callback_save(lua_State *L);
int __neko_bind_callback_call(lua_State *L);
int l_nameof(lua_State *L);

namespace detail {

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
struct LuaStack;

template <>
struct LuaStack<lua_CFunction> {
    static inline void Push(lua_State *L, lua_CFunction f) { lua_pushcfunction(L, f); }
    static inline lua_CFunction Get(lua_State *L, int index) { return lua_tocfunction(L, index); }
};

template <>
struct LuaStack<int> {
    static inline void Push(lua_State *L, int value) { lua_pushinteger(L, static_cast<lua_Integer>(value)); }
    static inline int Get(lua_State *L, int index) { return static_cast<int>(luaL_checkinteger(L, index)); }
};

template <>
struct LuaStack<int const &> {
    static inline void Push(lua_State *L, int value) { lua_pushnumber(L, static_cast<lua_Number>(value)); }
    static inline int Get(lua_State *L, int index) { return static_cast<int>(luaL_checknumber(L, index)); }
};

template <>
struct LuaStack<unsigned int> {
    static inline void Push(lua_State *L, unsigned int value) { lua_pushinteger(L, static_cast<lua_Integer>(value)); }
    static inline unsigned int Get(lua_State *L, int index) { return static_cast<unsigned int>(luaL_checkinteger(L, index)); }
};

template <>
struct LuaStack<unsigned int const &> {
    static inline void Push(lua_State *L, unsigned int value) { lua_pushnumber(L, static_cast<lua_Number>(value)); }
    static inline unsigned int Get(lua_State *L, int index) { return static_cast<unsigned int>(luaL_checknumber(L, index)); }
};

template <>
struct LuaStack<unsigned char> {
    static inline void Push(lua_State *L, unsigned char value) { lua_pushinteger(L, static_cast<lua_Integer>(value)); }
    static inline unsigned char Get(lua_State *L, int index) { return static_cast<unsigned char>(luaL_checkinteger(L, index)); }
};

template <>
struct LuaStack<unsigned char const &> {
    static inline void Push(lua_State *L, unsigned char value) { lua_pushnumber(L, static_cast<lua_Number>(value)); }
    static inline unsigned char Get(lua_State *L, int index) { return static_cast<unsigned char>(luaL_checknumber(L, index)); }
};

template <>
struct LuaStack<short> {
    static inline void Push(lua_State *L, short value) { lua_pushinteger(L, static_cast<lua_Integer>(value)); }
    static inline short Get(lua_State *L, int index) { return static_cast<short>(luaL_checkinteger(L, index)); }
};

template <>
struct LuaStack<short const &> {
    static inline void Push(lua_State *L, short value) { lua_pushnumber(L, static_cast<lua_Number>(value)); }
    static inline short Get(lua_State *L, int index) { return static_cast<short>(luaL_checknumber(L, index)); }
};

template <>
struct LuaStack<unsigned short> {
    static inline void Push(lua_State *L, unsigned short value) { lua_pushinteger(L, static_cast<lua_Integer>(value)); }
    static inline unsigned short Get(lua_State *L, int index) { return static_cast<unsigned short>(luaL_checkinteger(L, index)); }
};

template <>
struct LuaStack<unsigned short const &> {
    static inline void Push(lua_State *L, unsigned short value) { lua_pushnumber(L, static_cast<lua_Number>(value)); }
    static inline unsigned short Get(lua_State *L, int index) { return static_cast<unsigned short>(luaL_checknumber(L, index)); }
};

template <>
struct LuaStack<long> {
    static inline void Push(lua_State *L, long value) { lua_pushinteger(L, static_cast<lua_Integer>(value)); }
    static inline long Get(lua_State *L, int index) { return static_cast<long>(luaL_checkinteger(L, index)); }
};

template <>
struct LuaStack<long const &> {
    static inline void Push(lua_State *L, long value) { lua_pushnumber(L, static_cast<lua_Number>(value)); }
    static inline long Get(lua_State *L, int index) { return static_cast<long>(luaL_checknumber(L, index)); }
};

template <>
struct LuaStack<unsigned long> {
    static inline void Push(lua_State *L, unsigned long value) { lua_pushinteger(L, static_cast<lua_Integer>(value)); }
    static inline unsigned long Get(lua_State *L, int index) { return static_cast<unsigned long>(luaL_checkinteger(L, index)); }
};

template <>
struct LuaStack<unsigned long const &> {
    static inline void Push(lua_State *L, unsigned long value) { lua_pushnumber(L, static_cast<lua_Number>(value)); }
    static inline unsigned long Get(lua_State *L, int index) { return static_cast<unsigned long>(luaL_checknumber(L, index)); }
};

template <>
struct LuaStack<float> {
    static inline void Push(lua_State *L, float value) { lua_pushnumber(L, static_cast<lua_Number>(value)); }
    static inline float Get(lua_State *L, int index) { return static_cast<float>(luaL_checknumber(L, index)); }
};

template <>
struct LuaStack<float const &> {
    static inline void Push(lua_State *L, float value) { lua_pushnumber(L, static_cast<lua_Number>(value)); }
    static inline float Get(lua_State *L, int index) { return static_cast<float>(luaL_checknumber(L, index)); }
};

template <>
struct LuaStack<double> {
    static inline void Push(lua_State *L, double value) { lua_pushnumber(L, static_cast<lua_Number>(value)); }
    static inline double Get(lua_State *L, int index) { return static_cast<double>(luaL_checknumber(L, index)); }
};

template <>
struct LuaStack<double const &> {
    static inline void Push(lua_State *L, double value) { lua_pushnumber(L, static_cast<lua_Number>(value)); }
    static inline double Get(lua_State *L, int index) { return static_cast<double>(luaL_checknumber(L, index)); }
};

template <>
struct LuaStack<bool> {
    static inline void Push(lua_State *L, bool value) { lua_pushboolean(L, value ? 1 : 0); }
    static inline bool Get(lua_State *L, int index) { return lua_toboolean(L, index) ? true : false; }
};

template <>
struct LuaStack<bool const &> {
    static inline void Push(lua_State *L, bool value) { lua_pushboolean(L, value ? 1 : 0); }
    static inline bool Get(lua_State *L, int index) { return lua_toboolean(L, index) ? true : false; }
};

template <>
struct LuaStack<char> {
    static inline void Push(lua_State *L, char value) {
        char str[2] = {value, 0};
        lua_pushstring(L, str);
    }

    static inline char Get(lua_State *L, int index) { return luaL_checkstring(L, index)[0]; }
};

template <>
struct LuaStack<char const &> {
    static inline void Push(lua_State *L, char value) {
        char str[2] = {value, 0};
        lua_pushstring(L, str);
    }

    static inline char Get(lua_State *L, int index) { return luaL_checkstring(L, index)[0]; }
};

template <>
struct LuaStack<char const *> {
    static inline void Push(lua_State *L, char const *str) {
        if (str != 0)
            lua_pushstring(L, str);
        else
            lua_pushnil(L);
    }

    static inline char const *Get(lua_State *L, int index) { return lua_isnil(L, index) ? 0 : luaL_checkstring(L, index); }
};

template <>
struct LuaStack<std::string> {
    static inline void Push(lua_State *L, std::string const &str) { lua_pushlstring(L, str.c_str(), str.size()); }

    static inline std::string Get(lua_State *L, int index) {
        size_t len;
        const char *str = luaL_checklstring(L, index, &len);
        return std::string(str, len);
    }
};

template <>
struct LuaStack<std::string const &> {
    static inline void Push(lua_State *L, std::string const &str) { lua_pushlstring(L, str.c_str(), str.size()); }

    static inline std::string Get(lua_State *L, int index) {
        size_t len;
        const char *str = luaL_checklstring(L, index, &len);
        return std::string(str, len);
    }
};

}  // namespace detail

template <typename T>
T raw_to(lua_State *L, int index) {
    if constexpr (std::is_integral_v<T>) {
        luaL_argcheck(L, lua_isnumber(L, index), index, "number expected");
        return static_cast<T>(lua_tointeger(L, index));
    } else if constexpr (std::same_as<T, f32> || std::same_as<T, f64>) {
        luaL_argcheck(L, lua_isnumber(L, index), index, "number expected");
        return static_cast<T>(lua_tonumber(L, index));
    } else if constexpr (std::same_as<T, String>) {
        // luaL_argcheck(L, lua_isstring(L, index), index, "Neko::String expected");
        return luax_check_string(L, index);
    } else if constexpr (std::same_as<T, const_str>) {
        luaL_argcheck(L, lua_isstring(L, index), index, "string expected");
        return lua_tostring(L, index);
    } else if constexpr (std::same_as<T, bool>) {
        luaL_argcheck(L, lua_isboolean(L, index), index, "boolean expected");
        return lua_toboolean(L, index) != 0;
    } else if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<T>(lua_topointer(L, index));
    } else {
        static_assert(std::is_same_v<T, void>, "Unsupported type for neko_lua_to");
    }
}

// 自动弹出栈元素 确保数量不变的辅助类
class LuaStackGuard {
public:
    explicit LuaStackGuard(lua_State *L) : m_L(L), m_count(::lua_gettop(L)) {}
    ~LuaStackGuard() {
        int n = ::lua_gettop(m_L);
        assert(n >= m_count);
        if (n > m_count) {
            ::lua_pop(m_L, (n - m_count));
        }
    }

private:
    lua_State *m_L;
    int m_count;
};

struct LuaNil {};

namespace detail {
template <>
struct LuaStack<LuaNil> {
    static inline void Push(lua_State *L, LuaNil const &any) { lua_pushnil(L); }
};
}  // namespace detail

class LuaRef;

class LuaRefBase {
protected:
    lua_State *L;
    int m_ref;
    struct FromStackIndex {};

    // 不应该直接使用
    explicit LuaRefBase(lua_State *L, FromStackIndex) : L(L) { m_ref = luaL_ref(L, LUA_REGISTRYINDEX); }
    explicit LuaRefBase(lua_State *L, int ref) : L(L), m_ref(ref) {}
    ~LuaRefBase() { luaL_unref(L, LUA_REGISTRYINDEX, m_ref); }

public:
    virtual void Push() const { lua_rawgeti(L, LUA_REGISTRYINDEX, m_ref); }

    std::string tostring() const {
        lua_getglobal(L, "tostring");
        Push();
        lua_call(L, 1, 1);
        const char *str = lua_tostring(L, 1);
        lua_pop(L, 1);
        return std::string(str);
    }

    int Type() const {
        int result;
        Push();
        result = lua_type(L, -1);
        lua_pop(L, 1);
        return result;
    }

    inline bool IsNil() const { return Type() == LUA_TNIL; }
    inline bool IsNumber() const { return Type() == LUA_TNUMBER; }
    inline bool IsString() const { return Type() == LUA_TSTRING; }
    inline bool IsTable() const { return Type() == LUA_TTABLE; }
    inline bool IsFunction() const { return Type() == LUA_TFUNCTION; }
    inline bool IsUserdata() const { return Type() == LUA_TUSERDATA; }
    inline bool IsThread() const { return Type() == LUA_TTHREAD; }
    inline bool IsLightUserdata() const { return Type() == LUA_TLIGHTUSERDATA; }
    inline bool IsBool() const { return Type() == LUA_TBOOLEAN; }

    template <typename... Args>
    inline LuaRef const operator()(Args... args) const;

    template <typename... Args>
    inline void Call(int ret, Args... args) const;

    template <typename T>
    void Append(T v) const {
        Push();
        size_t len = lua_rawlen(L, -1);
        detail::LuaStack<T>::Push(L, v);
        lua_rawseti(L, -2, ++len);
        lua_pop(L, 1);
    }

    template <typename T>
    T Cast() {
        LuaStackGuard p(L);
        Push();
        return detail::LuaStack<T>::Get(L, -1);
    }

    template <typename T>
    operator T() {
        return Cast<T>();
    }
};

template <typename K>
class LuaTableElement : public LuaRefBase {
    friend class LuaRef;

private:
    K m_key;

public:
    LuaTableElement(lua_State *L, K key) : LuaRefBase(L, FromStackIndex()), m_key(key) {}

    void Push() const override {
        lua_rawgeti(L, LUA_REGISTRYINDEX, m_ref);
        detail::LuaStack<K>::Push(L, (K)m_key);
        lua_gettable(L, -2);
        lua_remove(L, -2);
    }

    // 为该表/键分配一个新值
    template <typename T>
    LuaTableElement &operator=(T v) {
        LuaStackGuard p(L);
        lua_rawgeti(L, LUA_REGISTRYINDEX, m_ref);
        detail::LuaStack<K>::Push(L, m_key);
        detail::LuaStack<T>::Push(L, v);
        lua_settable(L, -3);
        return *this;
    }

    template <typename NK>
    LuaTableElement<NK> operator[](NK key) const {
        Push();
        return LuaTableElement<NK>(L, key);
    }
};

namespace detail {
template <typename K>
struct LuaStack<LuaTableElement<K>> {
    static inline void Push(lua_State *L, LuaTableElement<K> const &e) { e.Push(); }
};
}  // namespace detail

class LuaRef : public LuaRefBase {
    friend LuaRefBase;

private:
    explicit LuaRef(lua_State *L, FromStackIndex fs) : LuaRefBase(L, fs) {}

public:
    LuaRef(lua_State *L) : LuaRefBase(L, LUA_REFNIL) {}

    LuaRef(lua_State *L, const std::string &global) : LuaRefBase(L, LUA_REFNIL) {
        lua_getglobal(L, global.c_str());
        m_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    LuaRef(LuaRef const &other) : LuaRefBase(other.L, LUA_REFNIL) {
        other.Push();
        m_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    LuaRef(LuaRef &&other) noexcept : LuaRefBase(other.L, other.m_ref) { other.m_ref = LUA_REFNIL; }

    LuaRef &operator=(LuaRef &&other) noexcept {
        if (this == &other) return *this;

        std::swap(L, other.L);
        std::swap(m_ref, other.m_ref);

        return *this;
    }

    LuaRef &operator=(LuaRef const &other) {
        if (this == &other) return *this;
        luaL_unref(L, LUA_REGISTRYINDEX, m_ref);
        other.Push();
        L = other.L;
        m_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        return *this;
    }

    template <typename K>
    LuaRef &operator=(LuaTableElement<K> &&other) noexcept {
        luaL_unref(L, LUA_REGISTRYINDEX, m_ref);
        other.Push();
        L = other.L;
        m_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        return *this;
    }

    template <typename K>
    LuaRef &operator=(LuaTableElement<K> const &other) {
        luaL_unref(L, LUA_REGISTRYINDEX, m_ref);
        other.Push();
        L = other.L;
        m_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        return *this;
    }

    template <typename K>
    LuaTableElement<K> operator[](K key) const {
        Push();
        return LuaTableElement<K>(L, key);
    }

    static LuaRef FromStack(lua_State *L, int index = -1) {
        lua_pushvalue(L, index);
        return LuaRef(L, FromStackIndex());
    }

    static LuaRef NewTable(lua_State *L) {
        lua_newtable(L);
        return LuaRef(L, FromStackIndex());
    }

    static LuaRef GetGlobal(lua_State *L, char const *name) {
        lua_getglobal(L, name);
        return LuaRef(L, FromStackIndex());
    }
};

namespace detail {
template <>
struct LuaStack<LuaRef> {
    static inline void Push(lua_State *L, LuaRef const &r) { r.Push(); }
};
}  // namespace detail

template <>
inline LuaRef const LuaRefBase::operator()() const {
    Push();
    luax_pcall(L, 0, 1);
    return LuaRef(L, FromStackIndex());
}

template <typename... Args>
inline LuaRef const LuaRefBase::operator()(Args... args) const {
    const int n = sizeof...(Args);
    Push();
    int dummy[] = {0, ((void)detail::LuaStack<Args>::Push(L, std::forward<Args>(args)), 0)...};
    std::ignore = dummy;
    luax_pcall(L, n, 1);
    return LuaRef(L, FromStackIndex());
}

template <>
inline void LuaRefBase::Call(int ret) const {
    Push();
    luax_pcall(L, 0, ret);
    return;  // 如果有返回值保留在 Lua 堆栈中
}

template <typename... Args>
inline void LuaRefBase::Call(int ret, Args... args) const {
    const int n = sizeof...(Args);
    Push();
    int dummy[] = {0, ((void)detail::LuaStack<Args>::Push(L, std::forward<Args>(args)), 0)...};
    std::ignore = dummy;
    luax_pcall(L, n, ret);
    return;  // 如果有返回值保留在 Lua 堆栈中
}

template <>
inline LuaRef LuaRefBase::Cast() {
    Push();
    return LuaRef(L, FromStackIndex());
}

template <lua_CFunction func>
int Wrap(lua_State *L) {
    int result = 0;
    try {
        result = func(L);
    }
    // 将带有描述的异常转换为 lua_error
    catch (std::exception &e) {
        luaL_error(L, e.what());
    }
    // 重新抛出任何其他异常 (例如lua_error)
    catch (...) {
        throw;
    }
    return result;
}

struct LuaVM {

    struct Tools {

        static std::string_view PushFQN(lua_State *const L, int const t, int const last) {
            luaL_Buffer _b;
            luaL_buffinit(L, &_b);
            int i{1};
            for (; i < last; ++i) {
                lua_rawgeti(L, t, i);
                luaL_addvalue(&_b);
                luaL_addlstring(&_b, "/", 1);
            }
            if (i == last) {  // 添加最后一个值(如果间隔不为空)
                lua_rawgeti(L, t, i);
                luaL_addvalue(&_b);
            }
            luaL_pushresult(&_b);  // &b 此时会弹出 (替换为result)
            return lua_tostring(L, -1);
        }

        static void check_arg_count(lua_State *L, int expected) {
            int n = lua_gettop(L);
            if (n != expected) {
                luaL_error(L, "Got %d arguments, expected %d", n, expected);
                return;
            }
            return;
        };

        template <typename T>
        static void ForEachTable(lua_State *L, T f, int index, int indent = 0) {
            lua_pushnil(L);
            while (lua_next(L, index) != 0) {
                // for (int i = 0; i < indent; i++) f(" ");
                if (lua_type(L, -2) == LUA_TSTRING) {
                    f(index, lua_tostring(L, -2));
                } else if (lua_type(L, -2) == LUA_TNUMBER) {
                    f(index, lua_tonumber(L, -2));
                } else {
                    f(index, lua_typename(L, lua_type(L, -2)));
                }

                if (lua_type(L, -1) == LUA_TSTRING) {
                    f(index, lua_tostring(L, -1));
                } else if (lua_type(L, -1) == LUA_TNUMBER) {
                    f(index, lua_tonumber(L, -1));
                } else if (lua_type(L, -1) == LUA_TTABLE) {
                    f(index, "table");
                    ForEachTable(L, f, lua_gettop(L), indent + 1);
                } else {
                    f(index, lua_typename(L, lua_type(L, -1)));
                }
                lua_pop(L, 1);
            }
        }

        template <typename T>
        static void ForEachStack(lua_State *L, T f) {
            int top = lua_gettop(L);
            for (int i = 1; i <= top; i++) {
                int t = lua_type(L, i);
                switch (t) {
                    case LUA_TSTRING:
                        f(i, lua_tostring(L, i));
                        break;
                    case LUA_TBOOLEAN:
                        f(i, lua_toboolean(L, i) ? "true" : "false");
                        break;
                    case LUA_TNUMBER:
                        f(i, lua_tonumber(L, i));
                        break;
                    case LUA_TTABLE:
                        f(i, "table:");
                        ForEachTable(L, f, i);
                        break;
                    default:
                        f(i, lua_typename(L, t));
                        break;
                }
            }
        }
    };

    static void *Allocf(void *ud, void *ptr, size_t osize, size_t nsize) {
        static size_t lua_mem_usage;
        if (!ptr) osize = 0;
        if (!nsize) {
            lua_mem_usage -= osize;
            free(ptr);
            return NULL;
        }
        lua_mem_usage += (nsize - osize);
        return realloc(ptr, nsize);
    }

    lua_State *L;

    inline lua_State *Create() {

#ifdef _DEBUG
        lua_State *L = ::lua_newstate(Allocf, NULL);
#else
        lua_State *L = ::luaL_newstate();
#endif

        ::luaL_openlibs(L);

#ifdef NEKO_CFFI
        lua_gc(L, LUA_GCSETPAUSE, 150);
#endif

        lua_pushinteger(L, 0);
        lua_setfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "type_index");
        lua_newtable(L);
        lua_setfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "type_ids");
        lua_newtable(L);
        lua_setfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "type_names");
        lua_newtable(L);
        lua_setfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "type_sizes");

        lua_newtable(L);
        lua_setfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "stack_push");
        lua_newtable(L);
        lua_setfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "stack_to");

        lua_newtable(L);
        lua_setfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "enums");
        lua_newtable(L);
        lua_setfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "enums_sizes");
        lua_newtable(L);
        lua_setfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "enums_values");

        luaL_Reg builtin_funcs[] = {
                {"nameof", Wrap<l_nameof>},
        };

        for (auto f : builtin_funcs) {
            lua_register(L, f.name, f.func);
        }

        this->L = L;

        return L;
    }

    inline void Fini(lua_State *L) {
        if (L) {

            lua_pushnil(L);
            lua_setfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "type_index");
            lua_pushnil(L);
            lua_setfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "type_ids");
            lua_pushnil(L);
            lua_setfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "type_names");
            lua_pushnil(L);
            lua_setfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "type_sizes");

            lua_pushnil(L);
            lua_setfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "stack_push");
            lua_pushnil(L);
            lua_setfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "stack_to");

            lua_pushnil(L);
            lua_setfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "enums");
            lua_pushnil(L);
            lua_setfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "enums_sizes");
            lua_pushnil(L);
            lua_setfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "enums_values");

            int top = lua_gettop(L);
            if (top != 0) {
                Tools::ForEachStack(L, []<typename T>(int i, T v) -> int {
                    std::cout << i << ':' << v << std::endl;
                    return 0;
                });
                printf("luastack memory leak\n");
            }
            ::lua_close(L);
        }
    }

    // template <typename T>
    operator lua_State *() { return L; }
};

inline void lua_run_string(lua_State *m_ls, const_str str_) {
    if (luaL_dostring(m_ls, str_)) {
        std::string err = lua_tostring(m_ls, -1);
        ::lua_pop(m_ls, 1);
        // printf("%s", err.c_str());
    }
}

inline void lua_run_string(lua_State *_L, const std::string &str_) { lua_run_string(_L, str_.c_str()); }

inline int neko_lua_load_file(lua_State *_L, const std::string &file_name_)  //
{
    if (luaL_dofile(_L, file_name_.c_str())) {
        std::string err = lua_tostring(_L, -1);
        ::lua_pop(_L, 1);
        // console_log("%s", err.c_str());
    }

    return 0;
}

inline void neko_lua_call(lua_State *_L, const char *func_name_) {
    lua_getglobal(_L, func_name_);

    if (lua_pcall(_L, 0, 0, 0) != 0) {
        std::string err = lua_tostring(_L, -1);
        ::lua_pop(_L, 1);
        // console_log("%s", err.c_str());
    }
}

namespace luavalue {
template <class>
inline constexpr bool always_false_v = false;

using value = std::variant<std::monostate,  // LUA_TNIL
                           bool,            // LUA_TBOOLEAN
                           void *,          // LUA_TLIGHTUSERDATA
                           lua_Integer,     // LUA_TNUMBER
                           lua_Number,      // LUA_TNUMBER
                           std::string,     // LUA_TSTRING
                           lua_CFunction    // LUA_TFUNCTION
                           >;
using table = std::map<std::string, value>;

void Set(lua_State *L, int idx, value &v);
void Set(lua_State *L, int idx, table &v);
void Get(lua_State *L, const value &v);
void Get(lua_State *L, const table &v);
}  // namespace luavalue

int vfs_lua_loader(lua_State *L);

#define LUASTRUCT_REQUIRED 1
#define LUASTRUCT_OPTIONAL 0

#define IS_STRUCT(L, index, type) LuaStructIs(L, #type, index)
#define CHECK_STRUCT(L, index, type) (LuaStructTodata<type>(L, index, LUASTRUCT_REQUIRED))
#define OPTIONAL_STRUCT(L, index, type) (LuaStructTodata<type>(L, index, LUASTRUCT_OPTIONAL))

#define PUSH_STRUCT(L, type, value)           \
    do {                                      \
        LuaStructNew(L, #type, sizeof(type)); \
        *CHECK_STRUCT(L, -1, type) = (value); \
    } while (0)

int LuaStructNew(lua_State *L, const char *metatable, size_t size);
int LuaStructNewRef(lua_State *L, const char *metatable, int parentIndex, const void *data);
int LuaStructIs(lua_State *L, const char *metatable, int index);
int LuaStructGC(lua_State *L, const char *metatable);
const char *LuaStructFieldname(lua_State *L, int index, size_t *length);

struct LUASTRUCT_CDATA {
    int ref;
    size_t cdata_size;
    const_str type_name;
};

template <typename T>
auto LuaStructTodata_w(lua_State *L, const char *metatable, int index, int required) -> T * {
    if (required == LUASTRUCT_OPTIONAL && lua_isnoneornil(L, index)) {
        return NULL;
    }
    LUASTRUCT_CDATA *reference = (LUASTRUCT_CDATA *)luaL_checkudata(L, index, metatable);
    if (reference->ref == LUA_NOREF) {
        return (T *)(reference + 1);
    } else {
        return *((T **)(reference + 1));
    }
}

template <typename T>
auto LuaStructTodata(lua_State *L, int index, int required) -> T * {
    const char *typeName = reflection::GetTypeName<T>();
    return LuaStructTodata_w<T>(L, typeName, index, LUASTRUCT_REQUIRED);
}

template <typename T>
struct is_struct {
    static constexpr bool value = std::is_class<T>::value;
};

// template <typename T>
// struct TypeName {
//     static const_str Get() { return typeid(T).name(); }
// };

// template <typename T>
// static const char *TypeNameV = TypeName<typename std::type_identity<T>::type>::Get();

template <typename T>
struct LuaStructAccess {
    static inline int Get(lua_State *L, const char *fieldName, T *data, int parentIndex, int set, int valueIndex) {
        if constexpr (is_struct<T>::value) {
            if (set) {
                *data = *CHECK_STRUCT(L, valueIndex, T);
                return 0;
            } else {
                return LuaStructNewRef(L, reflection::GetTypeName<T>(), parentIndex, data);
            }
        } else {
            if (set) {
                *data = static_cast<T>(detail::LuaStack<T>::Get(L, valueIndex));
                return 0;
            } else {
                detail::LuaStack<T>::Push(L, *data);
                return 1;
            }
        }
    }
};

template <typename T>
inline void LuaStructCreate(lua_State *L, const char *fieldName, const char *type_name, size_t type_size, T fafunc) {

    using fieldaccess_func = T;

    // auto _new = std::bind(LuaStructNew, std::placeholders::_1, type_name, type_size);

    if (fieldName) {
        lua_createtable(L, 0, 0);
        // lua_pushinteger(L, type_size);
        // lua_setfield(L, -2, "type_size");
        // lua_pushstring(L, type_name);
        // lua_setfield(L, -2, "type_name");

        lua_pushinteger(L, type_size);
        lua_pushstring(L, type_name);

        lua_pushcclosure(
                L,
                [](lua_State *L) -> int {
                    size_t _type_size = lua_tointeger(L, lua_upvalueindex(1));
                    const char *_type_name = lua_tostring(L, lua_upvalueindex(2));
                    return LuaStructNew(L, _type_name, _type_size);
                },
                2);
        lua_setfield(L, -2, "new");
        lua_setfield(L, -2, fieldName);
    }

    // 创建实例元表
    luaL_newmetatable(L, type_name);

    lua_pushstring(L, type_name);
    lua_pushcclosure(
            L,
            [](lua_State *L) -> int {
                const char *_type_name = lua_tostring(L, lua_upvalueindex(1));
                return LuaStructGC(L, _type_name);
            },
            1);
    lua_setfield(L, -2, "__gc");

    lua_pushboolean(L, 0);
    lua_pushcclosure(L, fafunc, 1);
    lua_setfield(L, -2, "__index");

    lua_pushboolean(L, 1);
    lua_pushcclosure(L, fafunc, 1);
    lua_setfield(L, -2, "__newindex");

    lua_pop(L, 1);
}

template <typename T, std::size_t I>
int LuaStructField_w(lua_State *L, const char *typeName, const char *field, int set, T &v) {

    int index = 1;

    if constexpr (I < reflection::field_count<T>) {
        constexpr std::string_view name = reflection::field_name<T, I>;
        // lua_getfield(L, arg, name.data());

        if (name == field) {
            // printf("LUASTRUCT_FIELD_W %s.%s\n", typeName, field);
            auto &af = reflection::field_access<I>(v);
            return LuaStructAccess<reflection::field_type<T, I>>::Get(L, field, &af, index, set, index + 2);
        }

        // reflection::field_access<I>(v) = unpack<reflection::field_type<D, I>>(L, -1);
        // lua_pop(L, 1);
        return LuaStructField_w<T, I + 1>(L, typeName, field, set, v);
    }

    return luaL_error(L, "Invalid field %s.%s", typeName, field);
};

template <typename T>
void LuaStruct(lua_State *L, const char *fieldName) {

    auto fieldaccess = [](lua_State *L) -> int {
        int index = 1;
        int set = lua_toboolean(L, lua_upvalueindex(1));

        // printf("fieldaccess %d\n", set);

        const char *typeName = reflection::GetTypeName<T>();
        T *data = (LuaStructTodata<T>(L, index, LUASTRUCT_REQUIRED));
        size_t length = 0;
        const char *field = LuaStructFieldname(L, index + 1, &length);

        // printf("fieldaccess %s\n", field);

        return LuaStructField_w<T, 0>(L, typeName, field, set, *data);
    };

    LuaStructCreate(L, fieldName, reflection::GetTypeName<T>(), sizeof(T), fieldaccess);

    // lua_setglobal(L, fieldName);
}

// enum

enum { NEKOLUA_INVALID_TYPE = -1 };

typedef lua_Integer LuaTypeid;
typedef int (*neko_luabind_Pushfunc)(lua_State *, LuaTypeid, const void *);
typedef void (*neko_luabind_Tofunc)(lua_State *, LuaTypeid, void *, int);

struct LuaTypeinfo {
    const char *name;
    size_t size;
};

template <typename T>
LuaTypeid LuaType(lua_State *L) {

    const char *type = reflection::GetTypeName<T>();
    constexpr size_t size = sizeof(T);

    lua_getfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "type_ids");
    lua_getfield(L, -1, type);

    if (lua_isnumber(L, -1)) {

        LuaTypeid id = lua_tointeger(L, -1);
        lua_pop(L, 2);
        return id;

    } else {

        lua_pop(L, 2);

        lua_getfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "type_index");

        LuaTypeid id = lua_tointeger(L, -1);
        lua_pop(L, 1);
        id++;

        lua_pushinteger(L, id);
        lua_setfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "type_index");

        lua_getfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "type_ids");
        lua_pushinteger(L, id);
        lua_setfield(L, -2, type);
        lua_pop(L, 1);

        lua_getfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "type_names");
        lua_pushinteger(L, id);
        lua_pushstring(L, type);
        lua_settable(L, -3);
        lua_pop(L, 1);

        lua_getfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "type_sizes");
        lua_pushinteger(L, id);
        lua_pushinteger(L, size);
        lua_settable(L, -3);
        lua_pop(L, 1);

        return id;
    }
}

inline auto TypeFind(lua_State *L, const char *type) -> LuaTypeid {
    lua_getfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "type_ids");
    lua_getfield(L, -1, type);
    LuaTypeid id = lua_isnil(L, -1) ? NEKOLUA_INVALID_TYPE : lua_tointeger(L, -1);
    lua_pop(L, 2);
    return id;
}

template <typename T>
inline LuaTypeinfo GetLuaTypeinfo(lua_State *L, T id);

template <>
inline LuaTypeinfo GetLuaTypeinfo(lua_State *L, LuaTypeid id) {

    LuaTypeinfo typeinfo{};

    lua_getfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "type_names");
    lua_pushinteger(L, id);
    lua_gettable(L, -2);

    typeinfo.name = lua_isnil(L, -1) ? "NEKOLUA_INVALID_TYPE" : lua_tostring(L, -1);
    lua_pop(L, 2);

    lua_getfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "type_sizes");
    lua_pushinteger(L, id);
    lua_gettable(L, -2);

    typeinfo.size = lua_isnil(L, -1) ? -1 : lua_tointeger(L, -1);
    lua_pop(L, 2);

    return typeinfo;
}

template <>
inline LuaTypeinfo GetLuaTypeinfo(lua_State *L, const char *name) {
    return GetLuaTypeinfo(L, TypeFind(L, name));
}

#define neko_lua_enum_has_value(L, type, value)                \
    const type __neko_lua_enum_value_temp_##value[] = {value}; \
    neko_lua_enum_has_value_type(L, LuaType<type>(L), __neko_lua_enum_value_temp_##value)

template <typename T, typename V>
    requires std::is_same_v<T, LuaTypeid>
inline bool LuaEnumHas(lua_State *L, T type, V v) {
    using VT = std::remove_cv_t<V>;
    if constexpr (std::is_integral_v<VT>) {
        lua_getfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "enums_values");
        lua_pushinteger(L, type);
        lua_gettable(L, -2);

        if (!lua_isnil(L, -1)) {

            lua_getfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "enums_sizes");
            lua_pushinteger(L, type);
            lua_gettable(L, -2);
            size_t size = lua_tointeger(L, -1);
            lua_pop(L, 2);

            // lua_Integer lvalue = 0;
            // memcpy(&lvalue, v, size);

            lua_pushinteger(L, v);
            lua_gettable(L, -2);

            if (lua_isnil(L, -1)) {
                lua_pop(L, 3);
                return false;
            } else {
                lua_pop(L, 3);
                return true;
            }
        }
        lua_pop(L, 2);
        lua_pushfstring(L, "neko_lua_enum_has_value: Enum '%s' not registered!", GetLuaTypeinfo(L, type).name);
        lua_error(L);
        return false;
    } else if constexpr (std::is_same_v<std::decay_t<VT>, char const *>) {
        lua_getfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "enums");
        lua_pushinteger(L, type);
        lua_gettable(L, -2);

        if (!lua_isnil(L, -1)) {

            lua_getfield(L, -1, v);

            if (lua_isnil(L, -1)) {
                lua_pop(L, 3);
                return false;
            } else {
                lua_pop(L, 3);
                return true;
            }
        }

        lua_pop(L, 2);
        lua_pushfstring(L, "LuaEnumHas: Enum '%s' not registered!", GetLuaTypeinfo(L, type).name);
        lua_error(L);
        return false;
    } else {
        static_assert(!v, "LuaEnumHas type assert");
    }
}

template <typename T, typename V>
inline bool LuaEnumHas(lua_State *L, V v) {
    return LuaEnumHas(L, LuaType<T>(L), v);
}

inline void LuaEnumAddType(lua_State *L, LuaTypeid type, size_t size) {
    lua_getfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "enums");
    lua_pushinteger(L, type);
    lua_newtable(L);
    lua_settable(L, -3);
    lua_pop(L, 1);

    lua_getfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "enums_values");
    lua_pushinteger(L, type);
    lua_newtable(L);
    lua_settable(L, -3);
    lua_pop(L, 1);

    lua_getfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "enums_sizes");
    lua_pushinteger(L, type);
    lua_pushinteger(L, size);
    lua_settable(L, -3);
    lua_pop(L, 1);
}

inline void LuaEnumAddValue(lua_State *L, LuaTypeid type, const void *value, const char *name) {

    lua_getfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "enums");
    lua_pushinteger(L, type);
    lua_gettable(L, -2);

    if (!lua_isnil(L, -1)) {

        lua_getfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "enums_sizes");
        lua_pushinteger(L, type);
        lua_gettable(L, -2);
        size_t size = lua_tointeger(L, -1);
        lua_pop(L, 2);

        lua_newtable(L);

        lua_Integer lvalue = 0;
        memcpy(&lvalue, value, size);

        lua_pushinteger(L, lvalue);
        lua_setfield(L, -2, "value");

        lua_pushstring(L, name);
        lua_setfield(L, -2, "name");

        lua_setfield(L, -2, name);

        lua_getfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "enums_values");
        lua_pushinteger(L, type);
        lua_gettable(L, -2);
        lua_pushinteger(L, lvalue);
        lua_getfield(L, -4, name);
        lua_settable(L, -3);

        lua_pop(L, 4);
        return;
    }

    lua_pop(L, 2);
    lua_pushfstring(L, "LuaEnumAddValue: Enum '%s' not registered!", GetLuaTypeinfo(L, type).name);
    lua_error(L);
}

inline bool LuaTypeIsEnum(lua_State *L, LuaTypeid type) {
    lua_getfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "enums");
    lua_pushinteger(L, type);
    lua_gettable(L, -2);
    bool reg = !lua_isnil(L, -1);
    lua_pop(L, 2);
    return reg;
}

template <typename Enum, int min_value = -64, int max_value = 64>
void LuaEnum(lua_State *L) {
    std::map<int, std::string> values;
    reflection::guess_enum_range<Enum, min_value>(values, std::make_integer_sequence<int, max_value - min_value>());
    reflection::guess_enum_bit_range<Enum>(values, std::make_integer_sequence<int, 32>());

    LuaTypeid id = LuaType<Enum>(L);
    LuaEnumAddType(L, id, sizeof(Enum));
    for (const auto &value : values) {
        const Enum enum_value[] = {(Enum)value.first};
        LuaEnumAddValue(L, id, enum_value, value.second.c_str());
    }
}

template <typename T>
inline void LuaPush(lua_State *L, const T &v) {
    detail::LuaStack<T>::Push(L, v);
}

template <typename T>
inline T LuaGet(lua_State *L, int index) {
    return detail::LuaStack<T>::Get(L, index);
}

template <typename T>
    requires std::is_enum_v<T>
inline void LuaPush(lua_State *L, const T &v) {
    LuaTypePush(L, LuaType<T>(L), &v);
}

template <typename T>
    requires std::is_enum_v<T>
inline auto LuaGet(lua_State *L, int index) -> T {
    T type_val;
    LuaTypeTo(L, LuaType<T>(L), &type_val, index);
    return type_val;
}

template <typename T, std::size_t I>
void LuaRawStructGet(lua_State *L, int arg, T &v);

template <typename T>
    requires std::is_pointer_v<T>
T LuaGet(lua_State *L, int arg) {
    luaL_checktype(L, arg, LUA_TLIGHTUSERDATA);
    return static_cast<T>(lua_touserdata(L, arg));
}

template <>
inline std::string_view LuaGet<std::string_view>(lua_State *L, int arg) {
    size_t sz = 0;
    const char *str = luaL_checklstring(L, arg, &sz);
    return std::string_view(str, sz);
}

template <typename T>
    requires std::is_aggregate_v<T>
T LuaGet(lua_State *L, int arg) {
    T v;
    LuaRawStructGet<T, 0>(L, arg, v);
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
T LuaGet(lua_State *L, int arg) {
    arg = lua_absindex(L, arg);
    luaL_checktype(L, arg, LUA_TTABLE);
    T v;
    lua_pushnil(L);
    while (lua_next(L, arg)) {
        auto key = LuaGet<typename T::key_type>(L, -2);
        auto mapped = LuaGet<typename T::mapped_type>(L, -1);
        v.emplace(std::move(key), std::move(mapped));
        lua_pop(L, 1);
    }
    return v;
}

template <typename T>
    requires is_instantiation_of<T, std::vector>
T LuaGet(lua_State *L, int arg) {
    arg = lua_absindex(L, arg);
    luaL_checktype(L, arg, LUA_TTABLE);
    lua_Integer n = luaL_len(L, arg);
    T v;
    v.reserve((size_t)n);
    for (lua_Integer i = 1; i <= n; ++i) {
        lua_geti(L, arg, i);
        auto value = LuaGet<typename T::value_type>(L, -1);
        v.emplace_back(std::move(value));
        lua_pop(L, 1);
    }
    return v;
}

template <typename T, std::size_t I>
void LuaRawStructGet(lua_State *L, int arg, T &v) {
    if constexpr (I < reflection::field_count<T>) {
        constexpr auto name = reflection::field_name<T, I>;
        lua_getfield(L, arg, name.data());
        reflection::field_access<I>(v) = LuaGet<reflection::field_type<T, I>>(L, -1);
        lua_pop(L, 1);
        LuaRawStructGet<T, I + 1>(L, arg, v);
    }
}

template <typename T, std::size_t I>
void LuaRawStructPush(lua_State *L, const T &v);

template <typename T>
    requires std::is_aggregate_v<T>
void LuaPush(lua_State *L, const T &v) {
    lua_createtable(L, 0, (int)reflection::field_count<T>);
    LuaRawStructPush<T, 0>(L, v);
}

template <typename T, std::size_t I>
void LuaRawStructPush(lua_State *L, const T &v) {
    if constexpr (I < reflection::field_count<T>) {
        constexpr auto name = reflection::field_name<T, I>;
        LuaPush<reflection::field_type<T, I>>(L, reflection::field_access<I>(v));
        lua_setfield(L, -2, name.data());
        LuaRawStructPush<T, I + 1>(L, v);
    }
}

inline int LuaTypePush(lua_State *L, LuaTypeid type_id, const void *c_in) {

    lua_getfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "stack_push");
    lua_pushinteger(L, type_id);
    lua_gettable(L, -2);

    if (!lua_isnil(L, -1)) {
        neko_luabind_Pushfunc func = (neko_luabind_Pushfunc)lua_touserdata(L, -1);
        lua_pop(L, 2);
        return func(L, type_id, c_in);
    }

    lua_pop(L, 2);

    // if (neko_luabind_struct_registered_type(L, type_id)) {
    //     return neko_luabind_struct_push_type(L, type_id, c_in);
    // }

    if (LuaTypeIsEnum(L, type_id)) {
        lua_getfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "enums_values");
        lua_pushinteger(L, type_id);
        lua_gettable(L, -2);

        if (!lua_isnil(L, -1)) {

            lua_getfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "enums_sizes");
            lua_pushinteger(L, type_id);
            lua_gettable(L, -2);
            size_t size = lua_tointeger(L, -1);
            lua_pop(L, 2);

            lua_Integer lvalue = 0;
            memcpy(&lvalue, c_in, size);

            lua_pushinteger(L, lvalue);
            lua_gettable(L, -2);

            if (!lua_isnil(L, -1)) {
                lua_getfield(L, -1, "name");
                lua_remove(L, -2);
                lua_remove(L, -2);
                lua_remove(L, -2);
                return 1;
            }

            lua_pop(L, 3);
            lua_pushfstring(L, "neko_lua_enum_push: Enum '%s' value %d not registered!", GetLuaTypeinfo(L, type_id).name, lvalue);
            lua_error(L);
            return 0;
        }

        lua_pop(L, 2);
        lua_pushfstring(L, "neko_lua_enum_push: Enum '%s' not registered!", GetLuaTypeinfo(L, type_id).name);
        lua_error(L);
        return 0;
    }

    lua_pushfstring(L, "neko_luabind_push: conversion to Lua object from type '%s' not registered!", GetLuaTypeinfo(L, type_id).name);
    lua_error(L);
    return 0;
}

inline void LuaTypeTo(lua_State *L, LuaTypeid type_id, void *c_out, int index) {

    lua_getfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "stack_to");
    lua_pushinteger(L, type_id);
    lua_gettable(L, -2);

    if (!lua_isnil(L, -1)) {
        neko_luabind_Tofunc func = (neko_luabind_Tofunc)lua_touserdata(L, -1);
        lua_pop(L, 2);
        func(L, type_id, c_out, index);
        return;
    }

    lua_pop(L, 2);

    // if (neko_luabind_struct_registered_type(L, type_id)) {
    //     neko_luabind_struct_to_type(L, type_id, c_out, index);
    //     return;
    // }

    if (LuaTypeIsEnum(L, type_id)) {

        const char *name = lua_tostring(L, index);

        lua_getfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "enums");
        lua_pushinteger(L, type_id);
        lua_gettable(L, -2);

        if (!lua_isnil(L, -1)) {

            lua_getfield(L, LUA_REGISTRYINDEX, NEKO_LUA_AUTO_REGISTER_PREFIX "enums_sizes");
            lua_pushinteger(L, type_id);
            lua_gettable(L, -2);
            size_t size = lua_tointeger(L, -1);
            lua_pop(L, 2);

            lua_pushstring(L, name);
            lua_gettable(L, -2);

            if (!lua_isnil(L, -1)) {
                lua_getfield(L, -1, "value");
                lua_Integer value = lua_tointeger(L, -1);
                lua_pop(L, 4);
                memcpy(c_out, &value, size);
                return;
            }

            lua_pop(L, 3);
            lua_pushfstring(L, "neko_lua_enum_to: Enum '%s' field '%s' not registered!", GetLuaTypeinfo(L, type_id).name, name);
            lua_error(L);
            return;
        }

        lua_pop(L, 3);
        lua_pushfstring(L, "neko_lua_enum_to: Enum '%s' not registered!", GetLuaTypeinfo(L, type_id).name);
        lua_error(L);
        return;
    }

    lua_pushfstring(L, "neko_luabind_to: conversion from Lua object to type '%s' not registered!", GetLuaTypeinfo(L, type_id).name);
    lua_error(L);
}

};  // namespace luabind

}  // namespace neko

enum W_LUA_UPVALUES { NEKO_W_COMPONENTS_NAME = 1, NEKO_W_UPVAL_N };

struct W_LUA_REGISTRY_CONST {
    static constexpr i32 W_CORE_IDX = 1;                             // neko_instance_t* index
    static constexpr const_str W_CORE = "__NEKO_W_CORE";             // neko_instance_t* reg
    static constexpr const_str ENG_UDATA_NAME = "__NEKO_ENGINE_UD";  // neko_instance_t* udata
    static constexpr const_str CVAR_MAP = "cvar_map";

    static constexpr i32 CVAR_MAP_MAX = 64;
};

#endif

#ifndef NEKO_LUABIND
#define NEKO_LUABIND

#include <bit>
#include <map>
#include <string>
#include <vector>

// #include "engine/base.hpp"

namespace std {
template <typename E, typename = std::enable_if_t<std::is_enum_v<E>>>
constexpr std::underlying_type_t<E> to_underlying(E e) noexcept {
    return static_cast<std::underlying_type_t<E>>(e);
}
}  // namespace std

namespace neko::lua {
struct callfunc {
    template <typename F, typename... Args>
    callfunc(F f, Args... args) {
        f(std::forward<Args>(args)...);
    }
};

inline auto &usermodules() {
    static std::vector<luaL_Reg> v;
    return v;
}

inline void register_module(const char *name, lua_CFunction func) { usermodules().emplace_back(luaL_Reg{name, func}); }

inline int preload_module(lua_State *L) {
    luaL_getsubtable(L, LUA_REGISTRYINDEX, "_PRELOAD");
    for (const auto &m : usermodules()) {
        lua_pushcfunction(L, m.func);
        lua_setfield(L, -2, m.name);
    }
    lua_pop(L, 1);
    return 0;
}
}  // namespace neko::lua

#define DEFINE_LUAOPEN(name)                                                           \
    int luaopen_neko_##name(lua_State *L) { return neko::lua::__##name ::luaopen(L); } \
    static ::neko::lua::callfunc __init_##name(::neko::lua::register_module, "__neko." #name, luaopen_neko_##name);

#define DEFINE_LUAOPEN_EXTERN(name) \
    namespace neko::lua::__##name { \
        int luaopen(lua_State *L);  \
    }                               \
    DEFINE_LUAOPEN(name)

namespace neko::lua {
inline std::string_view checkstrview(lua_State *L, int idx) {
    size_t len = 0;
    const char *buf = luaL_checklstring(L, idx, &len);
    return {buf, len};
}

template <typename T, typename I>
constexpr bool checklimit(I i) {
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
T checkinteger(lua_State *L, int arg) {
    static_assert(std::is_trivial_v<T>);
    if constexpr (std::is_enum_v<T>) {
        using UT = std::underlying_type_t<T>;
        return static_cast<T>(checkinteger<UT>(L, arg));
    } else if constexpr (std::is_integral_v<T>) {
        lua_Integer r = luaL_checkinteger(L, arg);
        if constexpr (std::is_same_v<T, lua_Integer>) {
            return r;
        } else if constexpr (sizeof(T) >= sizeof(lua_Integer)) {
            return static_cast<T>(r);
        } else {
            if (checklimit<T>(r)) {
                return static_cast<T>(r);
            }
            luaL_error(L, "bad argument '#%d' limit exceeded", arg);
            // std::unreachable();
            neko_assert(0, "unreachable");
        }
    } else {
        return std::bit_cast<T>(checkinteger<lua_Integer>(L, arg));
    }
}
template <typename T, T def>
T optinteger(lua_State *L, int arg) {
    static_assert(std::is_trivial_v<T>);
    if constexpr (std::is_enum_v<T>) {
        using UT = std::underlying_type_t<T>;
        return static_cast<T>(optinteger<UT, std::to_underlying(def)>(L, arg));
    } else if constexpr (std::is_integral_v<T>) {
        if constexpr (std::is_same_v<T, lua_Integer>) {
            return luaL_optinteger(L, arg, def);
        } else if constexpr (sizeof(T) == sizeof(lua_Integer)) {
            lua_Integer r = optinteger<lua_Integer, static_cast<lua_Integer>(def)>(L, arg);
            return static_cast<T>(r);
        } else if constexpr (sizeof(T) < sizeof(lua_Integer)) {
            lua_Integer r = optinteger<lua_Integer, static_cast<lua_Integer>(def)>(L, arg);
            if (checklimit<T>(r)) {
                return static_cast<T>(r);
            }
            luaL_error(L, "bad argument '#%d' limit exceeded", arg);
            // std::unreachable();
            neko_assert(0, "unreachable");
        } else {
            static_assert(checklimit<lua_Integer>(def));
            lua_Integer r = optinteger<lua_Integer, static_cast<lua_Integer>(def)>(L, arg);
            return static_cast<T>(r);
        }
    } else {
        // If std::bit_cast were not constexpr, it would fail here, so let it fail.
        return std::bit_cast<T>(optinteger<lua_Integer, std::bit_cast<lua_Integer>(def)>(L, arg));
    }
}

template <typename T>
T tolightud(lua_State *L, int arg) {
    if constexpr (std::is_integral_v<T>) {
        uintptr_t r = std::bit_cast<uintptr_t>(tolightud<void *>(L, arg));
        if constexpr (std::is_same_v<T, uintptr_t>) {
            return r;
        } else if constexpr (sizeof(T) >= sizeof(uintptr_t)) {
            return static_cast<T>(r);
        } else {
            if (checklimit<T>(r)) {
                return static_cast<T>(r);
            }
            luaL_error(L, "bad argument #%d limit exceeded", arg);
            // std::unreachable();
            neko_assert(0, "unreachable");
        }
    } else if constexpr (std::is_same_v<T, void *>) {
        return lua_touserdata(L, arg);
    } else if constexpr (std::is_pointer_v<T>) {
        return static_cast<T>(tolightud<void *>(L, arg));
    } else {
        return std::bit_cast<T>(tolightud<void *>(L, arg));
    }
}

union lua_maxalign_t {
    LUAI_MAXALIGN;
};
constexpr inline size_t lua_maxalign = std::alignment_of_v<lua_maxalign_t>;

template <typename T>
constexpr T *udata_align(void *storage) {
    if constexpr (std::alignment_of_v<T> > lua_maxalign) {
        uintptr_t mask = (uintptr_t)(std::alignment_of_v<T> - 1);
        storage = (void *)(((uintptr_t)storage + mask) & ~mask);
        return static_cast<T *>(storage);
    } else {
        return static_cast<T *>(storage);
    }
}

template <typename T>
constexpr T *udata_new(lua_State *L, int nupvalue) {
    if constexpr (std::alignment_of_v<T> > lua_maxalign) {
        void *storage = lua_newuserdatauv(L, sizeof(T) + std::alignment_of_v<T>, nupvalue);
        std::memset(storage, 0, sizeof(T));
        return udata_align<T>(storage);
    } else {
        void *storage = lua_newuserdatauv(L, sizeof(T), nupvalue);
        std::memset(storage, 0, sizeof(T));
        std::memset(storage, 0, sizeof(T));
        return udata_align<T>(storage);
    }
}

template <typename T>
T checklightud(lua_State *L, int arg) {
    luaL_checktype(L, arg, LUA_TLIGHTUSERDATA);
    return tolightud<T>(L, arg);
}

template <typename T>
T &toudata(lua_State *L, int arg) {
    return *udata_align<T>(lua_touserdata(L, arg));
}

template <typename T>
T *toudata_ptr(lua_State *L, int arg) {
    return udata_align<T>(lua_touserdata(L, arg));
}

template <typename T>
struct udata {};
template <typename T, typename = void>
struct udata_has_nupvalue : std::false_type {};
template <typename T>
struct udata_has_nupvalue<T, std::void_t<decltype(udata<T>::nupvalue)>> : std::true_type {};

template <typename T>
int destroyudata(lua_State *L) {
    toudata<T>(L, 1).~T();
    return 0;
}

template <typename T>
void getmetatable(lua_State *L) {
    if (luaL_newmetatable(L, reflection::name_v<T>.data())) {
        if constexpr (!std::is_trivially_destructible<T>::value) {
            lua_pushcfunction(L, destroyudata<T>);
            lua_setfield(L, -2, "__gc");
        }
        udata<T>::metatable(L);
    }
}

template <typename T, typename... Args>
T &newudata(lua_State *L, Args &&...args) {
    int nupvalue = 0;
    if constexpr (udata_has_nupvalue<T>::value) {
        nupvalue = udata<T>::nupvalue;
    }
    T *o = udata_new<T>(L, nupvalue);
    new (o) T(std::forward<Args>(args)...);
    getmetatable<T>(L);
    lua_setmetatable(L, -2);
    return *o;
}

template <typename T>
T &checkudata(lua_State *L, int arg, const_str tname = reflection::name_v<T>.data()) {
    return *udata_align<T>(luaL_checkudata(L, arg, tname));
}

}  // namespace neko::lua

#endif
