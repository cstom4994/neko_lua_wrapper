# neko_lua_wrapper

WIP

### Enum

```cpp
// Declaring a custom enumeration type in cpp
enum TestEnum : int {
    TestEnum_A,
    TestEnum_B,
    TestEnum_C,
};

// Automatically register it to lua
LuaEnum<TestEnum>(L);

// Some test functions
int TestEnumFunc_1(lua_State *L) {
    auto type_val = LuaGet<TestEnum>(L, 1);
    lua_pushinteger(L, type_val);
    return 1;
}

int TestEnumFunc_2(lua_State *L) {
    TestEnum type_val = (TestEnum)lua_tointeger(L, 1);
    LuaPush<TestEnum>(L, type_val);
    return 1;
}

lua_register(L, "TestEnumFunc_1", TestEnumFunc_1);
lua_register(L, "TestEnumFunc_2", TestEnumFunc_2);
```

```lua
-- Test in lua
print(TestEnumFunc_1("TestEnum_A")) -- 0
print(TestEnumFunc_2(2))            -- "TestEnum_C"
```

### Struct

```cpp
// Declaring a custom struct type in cpp
struct TestStruct {
    float x, y, z, w;
    int x1, x2;

    void print() { std::cout << x << ' ' << y << ' ' << z << ' ' << w << ' ' << x1 << ' ' << x2 << std::endl; }
};

lua_newtable(L);
LuaStruct<TestStruct>(L, "TestStruct"); // Automatically register it to lua
lua_setglobal(L, "LuaStruct");

// Some test functions
static int LuaStruct_test_1(lua_State *L) {
    auto v = LuaGet<TestStruct>(L, 1);
    v->x += 10.f;
    v->y += 10.f;
    v->z += 10.f;
    v->w += 10.f;
    LuaPush<TestStruct>(L, *v);
    return 1;
}

lua_register(L, "LuaStruct_test_1", LuaStruct_test_1);
```

```lua
-- Test in lua
test_struct = LuaStruct.TestStruct.new()
your_func(test_struct.x,test_struct.y,test_struct.z,test_struct.w) -- access struct elements directly by field name
test_struct = LuaStruct_test_1(test_struct)
your_func(test_struct.x,test_struct.y,test_struct.z,test_struct.w)
```