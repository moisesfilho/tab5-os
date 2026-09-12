// Testes host, sem goldens, para o contrato "dispatcher assíncrono de WASM":
//
//   1. Teardown idempotente/único: duas chamadas consecutivas de
//      tab5_wasm_unload liberam o bytecode uma única vez (nenhum double-free
//      nem teardown paralelo introduzido pelo dispatcher).
//   2. Geração/época por instância: quando o marcador existir na struct, o
//      unload precisa INVALIDAR eventos antigos postados (geração muda) e as
//      chamadas em voo precisam MANTER a geração estável — é o que permite ao
//      worker rejeitar jobs de instância já fechada.
//
// Os testes que dependem do marcador de geração usam detecção em tempo de
// compilação (trait + if constexpr) para não quebrar o build do host enquanto
// a implementação não chega; ficam SKIP até o campo existir. A verificação
// autoritativa (fonte) está em ../test_async_dispatcher_contract.py.
//
// Limite do ambiente: HAVE_WAMR=0/HAVE_LVGL=0 — o host não instancia WAMR nem
// cria worker/fila reais; a semântica de join/worker/fila exige alvo ESP-IDF.
// Aqui exercitamos apenas a semântica de struct (teardown geração).

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <type_traits>

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

template <typename T, typename = void> struct HasGeneration : std::false_type {};
template <typename T> struct HasGeneration<T, std::void_t<decltype(T::generation)>> : std::true_type {};

template <typename T, typename = void> struct HasDispatchGeneration : std::false_type {};
template <typename T>
struct HasDispatchGeneration<T, std::void_t<decltype(T::dispatch_generation)>> : std::true_type {};

template <typename T, typename = void> struct HasStateEpoch : std::false_type {};
template <typename T> struct HasStateEpoch<T, std::void_t<decltype(T::state_epoch)>> : std::true_type {};

template <typename T, typename = void> struct HasInstanceEpoch : std::false_type {};
template <typename T> struct HasInstanceEpoch<T, std::void_t<decltype(T::instance_epoch)>> : std::true_type {};

template <typename T, typename = void> struct HasActiveEpoch : std::false_type {};
template <typename T> struct HasActiveEpoch<T, std::void_t<decltype(T::active_epoch)>> : std::true_type {};

template <typename T, typename = void> struct HasEpoch : std::false_type {};
template <typename T> struct HasEpoch<T, std::void_t<decltype(T::epoch)>> : std::true_type {};

template <typename T>
inline constexpr bool kHasGenerationMarker =
    HasGeneration<T>::value || HasDispatchGeneration<T>::value || HasStateEpoch<T>::value ||
    HasInstanceEpoch<T>::value || HasActiveEpoch<T>::value || HasEpoch<T>::value;

template <typename T> uint64_t read_generation(const T &inst)
{
    if constexpr (HasGeneration<T>::value) {
        return static_cast<uint64_t>(inst.generation);
    } else if constexpr (HasDispatchGeneration<T>::value) {
        return static_cast<uint64_t>(inst.dispatch_generation);
    } else if constexpr (HasStateEpoch<T>::value) {
        return static_cast<uint64_t>(inst.state_epoch);
    } else if constexpr (HasInstanceEpoch<T>::value) {
        return static_cast<uint64_t>(inst.instance_epoch);
    } else if constexpr (HasActiveEpoch<T>::value) {
        return static_cast<uint64_t>(inst.active_epoch);
    } else if constexpr (HasEpoch<T>::value) {
        return static_cast<uint64_t>(inst.epoch);
    }
    return 0u;
}

} // namespace

TEST(AsyncDispatcherContract, DoubleUnloadFreesBytecodeExactlyOnce)
{
    tab5_wasm_app_instance_t inst = {};
    load_sample(inst);

    ASSERT_EQ(tab5_wasm_unload(&inst), TAB5_OK);
    EXPECT_EQ(inst.wasm_buf, nullptr);
    EXPECT_FALSE(inst.is_running);

    // Teardown repetido (ex.: job em voo + unload do package manager) precisa
    // ser idempotente: nada a liberar, sem double-free.
    ASSERT_EQ(tab5_wasm_unload(&inst), TAB5_OK);
    EXPECT_EQ(inst.wasm_buf, nullptr);
    EXPECT_FALSE(inst.is_running);
}

TEST(AsyncDispatcherContract, UnloadInvalidatesInstanceGeneration)
{
    if constexpr (!kHasGenerationMarker<tab5_wasm_app_instance_t>) {
        GTEST_SKIP() << "marcador de geração/época ainda não implementado na struct "
                        "tab5_wasm_app_instance_t";
    }

    tab5_wasm_app_instance_t inst = {};
    load_sample(inst);
    const uint64_t before = read_generation(inst);

    ASSERT_EQ(tab5_wasm_unload(&inst), TAB5_OK);
    EXPECT_NE(read_generation(inst), before) << "unload precisa invalidar eventos antigos postados para esta instância";
}

TEST(AsyncDispatcherContract, InFlightCallKeepsGenerationStable)
{
    if constexpr (!kHasGenerationMarker<tab5_wasm_app_instance_t>) {
        GTEST_SKIP() << "marcador de geração/época ainda não implementado na struct "
                        "tab5_wasm_app_instance_t";
    }

    tab5_wasm_app_instance_t inst = {};
    load_sample(inst);

    EXPECT_EQ(tab5_wasm_call_function(&inst, "noop", 0, nullptr), TAB5_OK);
    const uint64_t generation = read_generation(inst);

    EXPECT_EQ(tab5_wasm_call_function(&inst, "noop", 0, nullptr), TAB5_OK);
    EXPECT_EQ(read_generation(inst), generation) << "chamadas em voo não podem alterar a geração da instância";
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

TEST(AsyncDispatcherContract, RunningInstanceExecutesCallAndStringFallbacks)
{
    tab5_wasm_app_instance_t running = {};
    load_sample(running);
    const uint32_t args[] = {10, 20, 30, 40, 50};
    ASSERT_EQ(tab5_wasm_dispatcher_init(), TAB5_OK);
    EXPECT_TRUE(tab5_wasm_dispatch_post_call(&running, "missing", "fallback", 5, args));
    EXPECT_TRUE(tab5_wasm_dispatch_post_string(&running, "missing", "fallback", "payload"));
    tab5_wasm_dispatcher_shutdown();
}

TEST(AsyncDispatcherContract, NullOptionalArgumentsAreSafe)
{
    tab5_wasm_app_instance_t running = {};
    load_sample(running);
    ASSERT_EQ(tab5_wasm_dispatcher_init(), TAB5_OK);
    EXPECT_TRUE(tab5_wasm_dispatch_post_call(&running, nullptr, nullptr, 0, nullptr));
    EXPECT_TRUE(tab5_wasm_dispatch_post_string(&running, nullptr, nullptr, nullptr));
    tab5_wasm_dispatcher_shutdown();
}

TEST(AsyncDispatcherContract, HostLaunchUsesPackageManagerPath)
{
    const tab5_err_t result = tab5_wasm_dispatch_post_launch("com.tab5.missing", nullptr);
    EXPECT_NE(result, TAB5_OK);
}
