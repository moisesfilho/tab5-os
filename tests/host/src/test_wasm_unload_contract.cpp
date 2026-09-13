// Testes host para o contrato de unload durante uma admissão WAMR.

#include <gtest/gtest.h>

#include <atomic>
#include <cstring>
#include <thread>

#include "tab5_host_abi.h"
#include "tab5_wasm_runtime.h"

namespace {

void load_sample(tab5_wasm_app_instance_t &inst)
{
    uint8_t dummy_wasm[] = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    tab5_app_context_t ctx = {};
    strncpy(ctx.app_id, "com.tab5.unloadcontract", sizeof(ctx.app_id) - 1);
    ASSERT_EQ(tab5_wasm_load_from_bytes(dummy_wasm, sizeof(dummy_wasm), 16384, 65536, &ctx, &inst), TAB5_OK);
    ASSERT_NE(inst.wasm_buf, nullptr);
}

void check_unload_defers_while_admitted(tab5_wasm_app_instance_t &inst)
{
    load_sample(inst);
    uint32_t generation = 0;
    auto *token = tab5_wasm_dispatch_token_acquire(&inst, &generation);
    ASSERT_NE(token, nullptr);

    std::atomic<bool> unload_started = false;
    std::thread unloader([&] {
        unload_started.store(true, std::memory_order_release);
        EXPECT_EQ(tab5_wasm_unload(&inst), TAB5_OK);
    });
    while (!unload_started.load(std::memory_order_acquire))
        std::this_thread::yield();

    // The unload thread owns the lifecycle transition; the fixture never
    // writes lifecycle fields directly. It waits while this token is held.
    while (tab5_wasm_dispatch_token_validate(token, generation))
        std::this_thread::yield();
    EXPECT_TRUE(tab5_wasm_instance_unload_pending(&inst));
    EXPECT_FALSE(tab5_wasm_instance_is_running(&inst));
    EXPECT_NE(inst.wasm_buf, nullptr);

    tab5_wasm_dispatch_token_release(token);
    unloader.join();
    EXPECT_EQ(inst.wasm_buf, nullptr);
}

void check_idle_processing_frees_sample(tab5_wasm_app_instance_t &inst)
{
    load_sample(inst);
    uint32_t generation = 0;
    auto *token = tab5_wasm_dispatch_token_acquire(&inst, &generation);
    ASSERT_NE(token, nullptr);

    std::thread unloader([&] { EXPECT_EQ(tab5_wasm_unload(&inst), TAB5_OK); });
    while (tab5_wasm_dispatch_token_validate(token, generation))
        std::this_thread::yield();
    ASSERT_TRUE(tab5_wasm_instance_unload_pending(&inst));
    tab5_wasm_dispatch_token_release(token);
    unloader.join();

    EXPECT_EQ(inst.wasm_buf, nullptr) << "após a admissão sair o teardown precisa liberar o bytecode";
    EXPECT_FALSE(tab5_wasm_instance_is_running(&inst));
}

} // namespace

TEST(WasmUnloadContract, UnloadDuringActiveCallbackDefersTeardown)
{
    tab5_wasm_app_instance_t inst = {};
    check_unload_defers_while_admitted(inst);
}

TEST(WasmUnloadContract, IdleCallStackProcessesPendingTeardown)
{
    tab5_wasm_app_instance_t inst = {};
    check_idle_processing_frees_sample(inst);
}
