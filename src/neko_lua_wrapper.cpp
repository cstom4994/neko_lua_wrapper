#include "neko_lua_wrapper.hpp"

#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <algorithm>
#include <iostream>
#include <sstream>

// #include "engine/asset.h"
// #include "engine/base.h"
// #include "engine/base.hpp"
// #include "engine/bootstrap.h"
// #include "engine/scripting.h"

namespace neko::luabind {

namespace luavalue {
void Set(lua_State *L, int idx, value &v) {
    switch (lua_type(L, idx)) {
        case LUA_TNIL:
            v.emplace<std::monostate>();
            break;
        case LUA_TBOOLEAN:
            v.emplace<bool>(!!lua_toboolean(L, idx));
            break;
        case LUA_TLIGHTUSERDATA:
            v.emplace<void *>(lua_touserdata(L, idx));
            break;
        case LUA_TNUMBER:
            if (lua_isinteger(L, idx)) {
                v.emplace<lua_Integer>(lua_tointeger(L, idx));
            } else {
                v.emplace<lua_Number>(lua_tonumber(L, idx));
            }
            break;
        case LUA_TSTRING: {
            size_t sz = 0;
            const char *str = lua_tolstring(L, idx, &sz);
            v.emplace<std::string>(str, sz);
            break;
        }
        case LUA_TFUNCTION: {
            lua_CFunction func = lua_tocfunction(L, idx);
            if (func == NULL || lua_getupvalue(L, idx, 1) != NULL) {
                luaL_error(L, "Only light C function can be serialized");
                return;
            }
            v.emplace<lua_CFunction>(func);
            break;
        }
        default:
            luaL_error(L, "Unsupport type %s to serialize", lua_typename(L, idx));
    }
}

void Set(lua_State *L, int idx, table &t) {
    luaL_checktype(L, idx, LUA_TTABLE);
    lua_pushnil(L);
    while (lua_next(L, idx)) {
        size_t sz = 0;
        const char *str = luaL_checklstring(L, -2, &sz);
        std::pair<std::string, value> pair;
        pair.first.assign(str, sz);
        Set(L, -1, pair.second);
        t.emplace(pair);
        lua_pop(L, 1);
    }
}

void Get(lua_State *L, const value &v) {
    std::visit(
            [=](auto &&arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    lua_pushnil(L);
                } else if constexpr (std::is_same_v<T, bool>) {
                    lua_pushboolean(L, arg);
                } else if constexpr (std::is_same_v<T, void *>) {
                    lua_pushlightuserdata(L, arg);
                } else if constexpr (std::is_same_v<T, lua_Integer>) {
                    lua_pushinteger(L, arg);
                } else if constexpr (std::is_same_v<T, lua_Number>) {
                    lua_pushnumber(L, arg);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    lua_pushlstring(L, arg.data(), arg.size());
                } else if constexpr (std::is_same_v<T, lua_CFunction>) {
                    lua_pushcfunction(L, arg);
                } else {
                    static_assert(always_false_v<T>, "non-exhaustive visitor!");
                }
            },
            v);
}

void Get(lua_State *L, const table &t) {
    lua_createtable(L, 0, static_cast<int>(t.size()));
    for (const auto &[k, v] : t) {
        lua_pushlstring(L, k.data(), k.size());
        Get(L, v);
        lua_rawset(L, -3);
    }
}
}  // namespace luavalue

static int g_reference_table = LUA_NOREF;

// 从堆栈顶部弹出一个结构体实例
// 将结构体实例的引用表压入堆栈
static void affirmReferenceTable(lua_State *L) {
    int instanceIndex = lua_gettop(L);

    if (g_reference_table == LUA_NOREF) {
        // 创建全局参考表
        lua_newtable(L);

        // 创建元表
        lua_newtable(L);

        // 使用弱键 以便实例引用表自动清理
        lua_pushstring(L, "k");
        lua_setfield(L, -2, "__mode");

        // 在全局引用表上设置元表
        lua_setmetatable(L, -2);

        // 存储对全局引用表的引用
        g_reference_table = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    // 获取全局引用表
    lua_rawgeti(L, LUA_REGISTRYINDEX, g_reference_table);

    int globalIndex = lua_gettop(L);

    // 获取实例引用表
    lua_pushvalue(L, instanceIndex);
    lua_gettable(L, globalIndex);

    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);

        // 创建实例引用表
        lua_newtable(L);

        // 添加到全局参考表
        lua_pushvalue(L, instanceIndex);
        lua_pushvalue(L, -2);
        lua_settable(L, globalIndex);
    }

    // 将实例引用表移动到位并整理
    lua_replace(L, instanceIndex);
    lua_settop(L, instanceIndex);
}

static void LuaStruct_setmetatable(lua_State *L, const char *metatable, int index) {
    luaL_getmetatable(L, metatable);

    if (lua_isnoneornil(L, -1)) {
        luaL_error(L, "The metatable for %s has not been defined", metatable);
    }

    lua_setmetatable(L, index - 1);
}

int LuaStructNew(lua_State *L, const char *metatable, size_t size) {
    LUASTRUCT_CDATA *reference = (LUASTRUCT_CDATA *)lua_newuserdata(L, sizeof(LUASTRUCT_CDATA) + size);
    std::memset(reference, 0, sizeof(LUASTRUCT_CDATA) + size);
    reference->ref = LUA_NOREF;
    reference->cdata_size = size;
    reference->type_name = metatable;
    void *data = (void *)(reference + 1);
    memset(data, 0, size);
    LuaStruct_setmetatable(L, metatable, -1);
    return 1;
}

// ParentIndex 是包含对象的堆栈索引 或者 0 表示不包含对象
int LuaStructNewRef(lua_State *L, const char *metatable, int parentIndex, const void *data) {
    LUASTRUCT_CDATA *reference = (LUASTRUCT_CDATA *)lua_newuserdata(L, sizeof(LUASTRUCT_CDATA) + sizeof(data));

    if (parentIndex != 0) {
        // 存储对包含对象的引用
        lua_pushvalue(L, parentIndex);
        reference->ref = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        reference->ref = LUA_REFNIL;
    }

    *((const void **)(reference + 1)) = data;

    LuaStruct_setmetatable(L, metatable, -1);

    return 1;
}

int LuaStructGC(lua_State *L, const char *metatable) {
    LUASTRUCT_CDATA *reference = (LUASTRUCT_CDATA *)luaL_checkudata(L, 1, metatable);
    // printf("LuaStructGC %s %d %p\n", metatable, reference->ref, reference);
    luaL_unref(L, LUA_REGISTRYINDEX, reference->ref);
    return 0;
}

int LuaStructIs(lua_State *L, const char *metatable, int index) {
    if (lua_type(L, index) != LUA_TUSERDATA) {
        return 0;
    }
    lua_getmetatable(L, index);
    luaL_getmetatable(L, metatable);

    int metatablesMatch = lua_rawequal(L, -1, -2);

    lua_pop(L, 2);

    return metatablesMatch;
}

const char *LuaStructFieldname(lua_State *L, int index, size_t *length) {
    luaL_argcheck(L, lua_type(L, index) == LUA_TSTRING, index, "Field name must be a string");

    return lua_tolstring(L, index, length);
}

#define LuaStruct_access_u8 LuaStruct_access_uchar

// 从堆栈顶部弹出 struct userdata
// 将根结构体 userdata 压入堆栈
static int getRootStruct(lua_State *L) {
    int *reference = (int *)lua_touserdata(L, -1);

    if (!reference) {
        return luaL_error(L, "expected struct userdata at the top of the stack");
    }

    if (*reference == LUA_NOREF || *reference == LUA_REFNIL) {
        return 1;
    } else {
        lua_pop(L, 1);
        lua_rawgeti(L, LUA_REGISTRYINDEX, *reference);
        return getRootStruct(L);
    }
}

static int LuaStruct_access_cstring(lua_State *L, const char *fieldName, const char **data, int parentIndex, int set, int valueIndex) {
    if (set) {
        lua_pushvalue(L, valueIndex);
        *data = lua_tostring(L, -1);

        int copyIndex = lua_gettop(L);

        // 保留对 Lua 字符串的引用以防止垃圾回收
        lua_pushvalue(L, parentIndex);
        getRootStruct(L);
        affirmReferenceTable(L);

        if (*data) {
            lua_pushvalue(L, copyIndex);
        } else {
            lua_pushnil(L);
        }

        lua_setfield(L, -2, fieldName);

        return 0;
    } else {
        lua_pushstring(L, *data);
        return 1;
    }
}

void luax_stack_dump(lua_State *L) {
    i32 top = lua_gettop(L);
    printf("  --- lua stack (%d) ---\n", top);
    for (i32 i = 1; i <= top; i++) {
        printf("  [%d] (%s): ", i, luaL_typename(L, i));

        switch (lua_type(L, i)) {
            case LUA_TNUMBER:
                printf("%f\n", lua_tonumber(L, i));
                break;
            case LUA_TSTRING:
                printf("%s\n", lua_tostring(L, i));
                break;
            case LUA_TBOOLEAN:
                printf("%d\n", lua_toboolean(L, i));
                break;
            case LUA_TNIL:
                printf("nil\n");
                break;
            default:
                printf("%p\n", lua_topointer(L, i));
                break;
        }
    }
}

void luax_get(lua_State *L, const_str tb, const_str field) {
    lua_getglobal(L, tb);
    lua_getfield(L, -1, field);
    lua_remove(L, -2);
}

void luax_pcall(lua_State *L, i32 args, i32 results) {
    if (lua_pcall(L, args, results, 1) != LUA_OK) {
        lua_pop(L, 1);
    }
}

void luax_neko_get(lua_State *L, const char *field) {
    lua_getglobal(L, "neko");
    lua_getfield(L, -1, field);
    lua_remove(L, -2);
}

lua_Integer luax_len(lua_State *L, i32 arg) {
    lua_len(L, arg);
    lua_Integer len = luaL_checkinteger(L, -1);
    lua_pop(L, 1);
    return len;
}

void luax_geti(lua_State *L, i32 arg, lua_Integer n) {
    lua_pushinteger(L, n);
    lua_gettable(L, arg);
}

void luax_set_number_field(lua_State *L, const char *key, lua_Number n) {
    lua_pushnumber(L, n);
    lua_setfield(L, -2, key);
}

void luax_set_int_field(lua_State *L, const char *key, lua_Integer n) {
    lua_pushinteger(L, n);
    lua_setfield(L, -2, key);
}

void luax_set_string_field(lua_State *L, const char *key, const char *str) {
    lua_pushstring(L, str);
    lua_setfield(L, -2, key);
}

lua_Number luax_number_field(lua_State *L, i32 arg, const char *key) {
    lua_getfield(L, arg, key);
    lua_Number num = luaL_checknumber(L, -1);
    lua_pop(L, 1);
    return num;
}

lua_Number luax_opt_number_field(lua_State *L, i32 arg, const char *key, lua_Number fallback) {
    i32 type = lua_getfield(L, arg, key);

    lua_Number num = fallback;
    if (type != LUA_TNIL) {
        num = luaL_optnumber(L, -1, fallback);
    }

    lua_pop(L, 1);
    return num;
}

lua_Integer luax_int_field(lua_State *L, i32 arg, const char *key) {
    lua_getfield(L, arg, key);
    lua_Integer num = luaL_checkinteger(L, -1);
    lua_pop(L, 1);
    return num;
}

lua_Number luax_opt_int_field(lua_State *L, i32 arg, const char *key, lua_Number fallback) {
    i32 type = lua_getfield(L, arg, key);

    lua_Number num = fallback;
    if (type != LUA_TNIL) {
        num = luaL_optinteger(L, -1, fallback);
    }

    lua_pop(L, 1);
    return num;
}

String luax_string_field(lua_State *L, i32 arg, const char *key) {
    lua_getfield(L, arg, key);
    size_t len = 0;
    char *str = (char *)luaL_checklstring(L, -1, &len);
    lua_pop(L, 1);
    return {str, len};
}

String luax_opt_string_field(lua_State *L, i32 arg, const char *key, const char *fallback) {
    lua_getfield(L, arg, key);
    size_t len = 0;
    char *str = (char *)luaL_optlstring(L, -1, fallback, &len);
    lua_pop(L, 1);
    return {str, len};
}

bool luax_boolean_field(lua_State *L, i32 arg, const char *key, bool fallback) {
    i32 type = lua_getfield(L, arg, key);

    bool b = fallback;
    if (type != LUA_TNIL) {
        b = lua_toboolean(L, -1);
    }

    lua_pop(L, 1);
    return b;
}

String luax_check_string(lua_State *L, i32 arg) {
    size_t len = 0;
    char *str = (char *)luaL_checklstring(L, arg, &len);
    return {str, len};
}

String luax_opt_string(lua_State *L, i32 arg, String def) { return lua_isstring(L, arg) ? luax_check_string(L, arg) : def; }


int g_lua_callbacks_table_ref;  // LUA_NOREF

int __neko_bind_callback_save(lua_State *L) {
    const_str identifier = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    if (g_lua_callbacks_table_ref == LUA_NOREF) {
        lua_newtable(L);
        g_lua_callbacks_table_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, g_lua_callbacks_table_ref);
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, identifier);
    lua_pop(L, 1);
    lua_pop(L, 2);
    return 0;
}

int __neko_bind_callback_call(lua_State *L) {
    if (g_lua_callbacks_table_ref != LUA_NOREF) {
        const_str identifier = luaL_checkstring(L, 1);
        lua_rawgeti(L, LUA_REGISTRYINDEX, g_lua_callbacks_table_ref);
        lua_getfield(L, -1, identifier);
        if (lua_isfunction(L, -1)) {
            int nargs = lua_gettop(L) - 1;  // 获取参数数量 减去标识符参数
            for (int i = 2; i <= nargs + 1; ++i) {
                lua_pushvalue(L, i);
            }
            lua_call(L, nargs, 0);  // 调用
        } else {
            printf("callback with identifier '%s' not found or is not a function", identifier);
        }
        lua_pop(L, 1);
    } else {
        printf("callback table is noref");
    }
    return 0;
}

// 返回一些有助于识别对象的名称
[[nodiscard]] static int DiscoverObjectNameRecur(lua_State *L, int shortest_, int depth_) {
    static constexpr int kWhat{1};
    static constexpr int kResult{2};
    static constexpr int kCache{3};
    static constexpr int kFQN{4};

    if (shortest_ <= depth_ + 1) {
        return shortest_;
    }
    assert(lua_checkstack(L, 3));

    lua_pushvalue(L, -1);
    lua_rawget(L, kCache);

    if (!lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return shortest_;
    }

    lua_pop(L, 1);
    lua_pushvalue(L, -1);
    lua_pushinteger(L, 1);
    lua_rawset(L, kCache);

    lua_pushnil(L);
    while (lua_next(L, -2)) {

        ++depth_;
        lua_pushvalue(L, -2);
        lua_rawseti(L, kFQN, depth_);
        if (lua_rawequal(L, -1, kWhat)) {

            if (depth_ < shortest_) {
                shortest_ = depth_;
                std::ignore = neko::luabind::LuaVM::Tools::PushFQN(L, kFQN, depth_);
                lua_replace(L, kResult);
            }

            lua_pop(L, 2);
            break;
        }
        switch (lua_type(L, -1)) {
            default:
                break;

            case LUA_TTABLE:
                shortest_ = DiscoverObjectNameRecur(L, shortest_, depth_);

                if (lua_getmetatable(L, -1)) {
                    if (lua_istable(L, -1)) {
                        ++depth_;
                        lua_pushstring(L, "__metatable");
                        lua_rawseti(L, kFQN, depth_);
                        shortest_ = DiscoverObjectNameRecur(L, shortest_, depth_);
                        lua_pushnil(L);
                        lua_rawseti(L, kFQN, depth_);
                        --depth_;
                    }
                    lua_pop(L, 1);
                }
                break;

            case LUA_TTHREAD:

                break;

            case LUA_TUSERDATA:

                if (lua_getmetatable(L, -1)) {
                    if (lua_istable(L, -1)) {
                        ++depth_;
                        lua_pushstring(L, "__metatable");
                        lua_rawseti(L, kFQN, depth_);
                        shortest_ = DiscoverObjectNameRecur(L, shortest_, depth_);
                        lua_pushnil(L);
                        lua_rawseti(L, kFQN, depth_);
                        --depth_;
                    }
                    lua_pop(L, 1);
                }

                {
                    int _uvi{1};
                    while (lua_getiuservalue(L, -1, _uvi) != LUA_TNONE) {
                        if (lua_istable(L, -1)) {
                            ++depth_;
                            lua_pushstring(L, "uservalue");
                            lua_rawseti(L, kFQN, depth_);
                            shortest_ = DiscoverObjectNameRecur(L, shortest_, depth_);
                            lua_pushnil(L);
                            lua_rawseti(L, kFQN, depth_);
                            --depth_;
                        }
                        lua_pop(L, 1);
                        ++_uvi;
                    }

                    lua_pop(L, 1);
                }
                break;
        }

        lua_pop(L, 1);

        lua_pushnil(L);
        lua_rawseti(L, kFQN, depth_);
        --depth_;
    }

    lua_pushvalue(L, -1);
    lua_pushnil(L);
    lua_rawset(L, kCache);
    return shortest_;
}

int l_nameof(lua_State *L) {
    int const _what{lua_gettop(L)};
    if (_what > 1) {
        luaL_argerror(L, _what, "too many arguments.");
    }

    if (lua_type(L, 1) < LUA_TTABLE) {
        lua_pushstring(L, luaL_typename(L, 1));
        lua_insert(L, -2);
        return 2;
    }

    lua_pushnil(L);

    lua_newtable(L);  // 所有已访问表的缓存

    lua_newtable(L);  // 其内容是字符串 连接时会产生唯一的名称
    lua_pushstring(L, LUA_GNAME);
    lua_rawseti(L, -2, 1);

    lua_pushglobaltable(L);  // 开始搜索
    std::ignore = DiscoverObjectNameRecur(L, std::numeric_limits<int>::max(), 1);
    if (lua_isnil(L, 2)) {
        lua_pop(L, 1);
        lua_pushstring(L, "_R");
        lua_rawseti(L, -2, 1);
        lua_pushvalue(L, LUA_REGISTRYINDEX);
        std::ignore = DiscoverObjectNameRecur(L, std::numeric_limits<int>::max(), 1);
    }
    lua_pop(L, 3);
    lua_pushstring(L, luaL_typename(L, 1));
    lua_replace(L, -3);
    return 2;
}

}  // namespace neko::luabind

bool neko_lua_equal(lua_State *state, int index1, int index2) {
#if LUA_VERSION_NUM <= 501
    return lua_equal(state, index1, index2) == 1;
#else
    return lua_compare(state, index1, index2, LUA_OPEQ) == 1;
#endif
}

int neko_lua_preload(lua_State *L, lua_CFunction f, const char *name) {
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "preload");
    lua_pushcfunction(L, f);
    lua_setfield(L, -2, name);
    lua_pop(L, 2);
    return 0;
}

int neko_lua_preload_auto(lua_State *L, lua_CFunction f, const char *name) {
    neko_lua_preload(L, f, name);
    lua_getglobal(L, "require");
    lua_pushstring(L, name);
    lua_call(L, 1, 0);
    return 0;
}

void neko_lua_load(lua_State *L, const luaL_Reg *l, const char *name) {
    lua_getglobal(L, name);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
    }
    luaL_setfuncs(L, l, 0);
    lua_setglobal(L, name);
}

void neko_lua_loadover(lua_State *L, const luaL_Reg *l, const char *name) {
    lua_newtable(L);
    luaL_setfuncs(L, l, 0);
    lua_setglobal(L, name);
}

int neko_lua_get_table_pairs_count(lua_State *L, int index) {
    int count = 0;
    index = lua_absindex(L, index);
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        // 现在栈顶是value，下一个栈元素是key
        count++;
        lua_pop(L, 1);  // 移除value，保留key作为下一次迭代的key
    }
    return count;
}
