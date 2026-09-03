#include <gtest/gtest.h>
#include "tab5_wasm_runtime.h"
#include "tab5_host_abi.h"
#include <cstring>

TEST(WasmRuntimeTest, InitAndDestroy)
{
    EXPECT_EQ(tab5_wasm_runtime_init(), TAB5_OK);
    // Idempotent
    EXPECT_EQ(tab5_wasm_runtime_init(), TAB5_OK);
    tab5_wasm_runtime_destroy();
}

TEST(WasmRuntimeTest, LoadFromBytesValidation)
{
    tab5_wasm_app_instance_t inst = {};
    uint8_t dummy_wasm[] = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00}; // WASM Magic header

    EXPECT_EQ(tab5_wasm_load_from_bytes(nullptr, sizeof(dummy_wasm), 0, 0, nullptr, &inst), TAB5_ERR_INVALID_ARG);
    EXPECT_EQ(tab5_wasm_load_from_bytes(dummy_wasm, 0, 0, 0, nullptr, &inst), TAB5_ERR_INVALID_ARG);
    EXPECT_EQ(tab5_wasm_load_from_bytes(dummy_wasm, sizeof(dummy_wasm), 0, 0, nullptr, nullptr), TAB5_ERR_INVALID_ARG);

    tab5_app_context_t ctx = {};
    strncpy(ctx.app_id, "com.tab5.wasmtest", sizeof(ctx.app_id) - 1);

    EXPECT_EQ(tab5_wasm_load_from_bytes(dummy_wasm, sizeof(dummy_wasm), 16384, 65536, &ctx, &inst), TAB5_OK);
    EXPECT_STREQ(inst.app_id, "com.tab5.wasmtest");
    EXPECT_EQ(inst.host_ctx, &ctx);
    EXPECT_TRUE(inst.is_running);

    EXPECT_EQ(tab5_wasm_call_function(&inst, "app_main", 0, nullptr), TAB5_OK);

    EXPECT_EQ(tab5_wasm_unload(&inst), TAB5_OK);
    EXPECT_FALSE(inst.is_running);
    EXPECT_EQ(inst.wasm_buf, nullptr);
}
