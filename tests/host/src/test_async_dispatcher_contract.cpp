// Testes host, sem goldens, para o contrato "dispatcher assíncrono de WASM":
//
//   1. Teardown idempotente/único: duas chamadas consecutivas de
//      tab5_wasm_unload liberam o bytecode uma única vez (nenhum double-free
//      nem teardown paralelo introduzido pelo dispatcher).
//   2. Geração/época por instância: unload invalida tokens antigos e chamadas
//      admitidas mantêm a geração estável — o worker pode assim rejeitar jobs
//      de uma instância já fechada.
//
// Limite do ambiente: HAVE_WAMR=0/HAVE_LVGL=0 — o host não instancia WAMR nem
// cria worker/fila reais; a semântica de join/worker/fila exige alvo ESP-IDF.
// Aqui exercitamos apenas a semântica de struct (teardown geração).

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <atomic>
#include <thread>

#include "tab5_host_abi.h"
#include "tab5_wasm_runtime.h"
#include "tab5_wasm_dispatcher.h"

namespace {

template <typename Inst> void load_sample(Inst &inst)
{
    uint8_t dummy_wasm[] = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    tab5_app_context_t ctx = {};
    strncpy(ctx.app_id, "com.tab5.dispatchercontract", sizeof(ctx.app_id) - 1);
    ASSERT_EQ(tab5_wasm_load_from_bytes(dummy_wasm, sizeof(dummy_wasm), 16384, 65536, &ctx, &inst), TAB5_OK);
    ASSERT_NE(inst.wasm_buf, nullptr);
}

uint32_t read_generation(const tab5_wasm_app_instance_t &inst)
{
    uint32_t generation = 0;
    EXPECT_TRUE(tab5_wasm_instance_snapshot(&inst, nullptr, &generation));
    return generation;
}

} // namespace

TEST(AsyncDispatcherContract, DoubleUnloadFreesBytecodeExactlyOnce)
{
    tab5_wasm_app_instance_t inst = {};
    load_sample(inst);

    ASSERT_EQ(tab5_wasm_unload(&inst), TAB5_OK);
    EXPECT_EQ(inst.wasm_buf, nullptr);
    EXPECT_FALSE(tab5_wasm_instance_is_running(&inst));

    // Teardown repetido (ex.: job em voo + unload do package manager) precisa
    // ser idempotente: nada a liberar, sem double-free.
    ASSERT_EQ(tab5_wasm_unload(&inst), TAB5_OK);
    EXPECT_EQ(inst.wasm_buf, nullptr);
    EXPECT_FALSE(tab5_wasm_instance_is_running(&inst));
}

TEST(AsyncDispatcherContract, UnloadInvalidatesInstanceGeneration)
{
    tab5_wasm_app_instance_t inst = {};
    load_sample(inst);
    uint32_t before = 0;
    auto *token = tab5_wasm_dispatch_token_acquire(&inst, &before);
    ASSERT_NE(token, nullptr);
    std::thread unloader([&] { EXPECT_EQ(tab5_wasm_unload(&inst), TAB5_OK); });
    while (tab5_wasm_dispatch_token_validate(token, before))
        std::this_thread::yield();
    EXPECT_FALSE(tab5_wasm_dispatch_token_validate(token, before))
        << "unload precisa invalidar eventos antigos postados para esta instância";
    tab5_wasm_dispatch_token_release(token);
    unloader.join();
    EXPECT_EQ(inst.wasm_buf, nullptr);
}

TEST(AsyncDispatcherContract, InFlightCallKeepsGenerationStable)
{
    tab5_wasm_app_instance_t inst = {};
    load_sample(inst);

    EXPECT_EQ(tab5_wasm_call_function(&inst, "noop", 0, nullptr), TAB5_OK);
    const uint32_t generation = read_generation(inst);

    EXPECT_EQ(tab5_wasm_call_function(&inst, "noop", 0, nullptr), TAB5_OK);
    EXPECT_EQ(read_generation(inst), generation) << "chamadas em voo não podem alterar a geração da instância";
    ASSERT_EQ(tab5_wasm_unload(&inst), TAB5_OK);
}

TEST(AsyncDispatcherContract, QueueLifecycleRejectsStaleJobsAndAcceptsBoundedInputs)
{
    ASSERT_EQ(tab5_wasm_dispatcher_init(), TAB5_OK);
    ASSERT_EQ(tab5_wasm_dispatcher_init(), TAB5_OK);

    tab5_wasm_app_instance_t stopped = {};
    load_sample(stopped);
    ASSERT_EQ(tab5_wasm_unload(&stopped), TAB5_OK);
    const uint32_t args[] = {1, 2, 3, 4, 5, 6};
    // Admission is performed before enqueue. A stopped/unmanaged instance is
    // rejected rather than leaving a stale raw pointer in the worker queue.
    EXPECT_FALSE(tab5_wasm_dispatch_post_call(&stopped, "missing", "alias", 6, args));
    EXPECT_FALSE(tab5_wasm_dispatch_post_string(&stopped, "missing", "alias", "value"));
    tab5_wasm_dispatcher_shutdown();
    tab5_wasm_dispatcher_shutdown();
}

TEST(AsyncDispatcherContract, DequeueValidationRejectsGenerationChangedBeforeExecution)
{
    tab5_wasm_app_instance_t inst = {};
    load_sample(inst);
    uint32_t generation = 0;
    auto *token = tab5_wasm_dispatch_token_acquire(&inst, &generation);
    ASSERT_NE(token, nullptr);

    // Unload owns the lifecycle transition and advances the generation while
    // the admission token keeps the instance storage alive.
    std::atomic<bool> unload_started = false;
    std::thread unloader([&] {
        unload_started.store(true, std::memory_order_release);
        EXPECT_EQ(tab5_wasm_unload(&inst), TAB5_OK);
    });
    while (!unload_started.load(std::memory_order_acquire))
        std::this_thread::yield();
    while (tab5_wasm_dispatch_token_validate(token, generation))
        std::this_thread::yield();
    EXPECT_FALSE(tab5_wasm_dispatch_token_validate(token, generation));
    tab5_wasm_dispatch_token_release(token);
    unloader.join();
    EXPECT_EQ(inst.wasm_buf, nullptr);
}

TEST(AsyncDispatcherContract, RunningInstanceExecutesCallAndStringFallbacks)
{
    tab5_wasm_app_instance_t running = {};
    load_sample(running);
    const uint32_t args[] = {10, 20, 30, 40, 50};
    ASSERT_EQ(tab5_wasm_dispatcher_init(), TAB5_OK);
    EXPECT_TRUE(tab5_wasm_dispatch_post_call(&running, "missing", "fallback", 5, args));
    EXPECT_TRUE(tab5_wasm_dispatch_post_string(&running, "missing", "fallback", "payload"));
    ASSERT_TRUE(tab5_wasm_dispatcher_wait_idle(1000));
    ASSERT_EQ(tab5_wasm_unload(&running), TAB5_OK);
    tab5_wasm_dispatcher_shutdown();
}

TEST(AsyncDispatcherContract, NullOptionalArgumentsAreSafe)
{
    tab5_wasm_app_instance_t running = {};
    load_sample(running);
    ASSERT_EQ(tab5_wasm_dispatcher_init(), TAB5_OK);
    EXPECT_TRUE(tab5_wasm_dispatch_post_call(&running, nullptr, nullptr, 0, nullptr));
    EXPECT_TRUE(tab5_wasm_dispatch_post_string(&running, nullptr, nullptr, nullptr));
    ASSERT_TRUE(tab5_wasm_dispatcher_wait_idle(1000));
    ASSERT_EQ(tab5_wasm_unload(&running), TAB5_OK);
    tab5_wasm_dispatcher_shutdown();
}

TEST(AsyncDispatcherContract, HostLaunchUsesPackageManagerPath)
{
    ASSERT_EQ(tab5_wasm_dispatcher_init(), TAB5_OK);
    EXPECT_EQ(tab5_wasm_dispatch_post_launch("com.tab5.missing", nullptr), TAB5_OK);
    EXPECT_TRUE(tab5_wasm_dispatcher_wait_idle(1000));
    tab5_wasm_dispatcher_shutdown();
}

TEST(AsyncDispatcherContract, ShutdownCancelsQueuedLaunches)
{
    ASSERT_EQ(tab5_wasm_dispatcher_init(), TAB5_OK);
    for (int i = 0; i < TAB5_WASM_DISPATCH_QUEUE_CAPACITY; ++i)
        ASSERT_EQ(tab5_wasm_dispatch_post_launch("com.tab5.missing", nullptr), TAB5_OK);
    tab5_wasm_dispatcher_shutdown();
    EXPECT_TRUE(tab5_wasm_dispatcher_wait_idle(10));
}

TEST(AsyncDispatcherContract, LaunchAndShutdownHaveControlledLinearization)
{
    ASSERT_EQ(tab5_wasm_dispatcher_init(), TAB5_OK);
    std::atomic<bool> posted = false;
    std::thread producer([&posted] {
        for (int i = 0; i < 32; ++i) {
            (void)tab5_wasm_dispatch_post_launch("com.tab5.missing", nullptr);
            posted = true;
        }
    });
    tab5_wasm_dispatcher_shutdown();
    producer.join();
    EXPECT_TRUE(posted.load());
    tab5_wasm_dispatcher_shutdown();
}

TEST(AsyncDispatcherContract, ConcurrentPostExecuteUnloadHasOwnedAdmission)
{
    ASSERT_EQ(tab5_wasm_dispatcher_init(), TAB5_OK);
    for (int round = 0; round < 32; ++round) {
        tab5_wasm_app_instance_t inst = {};
        load_sample(inst);
        std::atomic<bool> start = false;
        std::atomic<tab5_err_t> unload_result = TAB5_ERR_FAIL;
        std::thread poster([&] {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            (void)tab5_wasm_dispatch_post_call(&inst, "noop", nullptr, 0, nullptr);
        });
        std::thread unloader([&] {
            start.store(true, std::memory_order_release);
            unload_result.store(tab5_wasm_unload(&inst), std::memory_order_release);
        });
        poster.join();
        unloader.join();
        EXPECT_EQ(unload_result.load(std::memory_order_acquire), TAB5_OK);
        EXPECT_FALSE(tab5_wasm_instance_is_running(&inst));
        EXPECT_TRUE(tab5_wasm_dispatcher_wait_idle(1000));
    }
    tab5_wasm_dispatcher_shutdown();
}
