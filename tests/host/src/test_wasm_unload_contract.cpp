// Testes host, sem goldens, para o contrato "unload durante callback WAMR":
//
//   1. call_depth/inflight por instância: enquanto inst.call_depth > 0 a
//      instância está com uma chamada WAMR ativa e o teardown é deferido.
//   2. unload/close pendente: tab5_wasm_unload marca unload_pending e NÃO
//      libera o bytecode (wasm_buf continua alocado).
//   3. processamento após join: com a pilha vazia (call_depth == 0), um novo
//      unload processa o encerramento pendente e libera wasm_buf.
//
// Estes testes dependem de campos ainda não presentes na struct
// tab5_wasm_app_instance_t. Usam detecção em tempo de compilação para não
// quebrar o build do host enquanto a implementação não chega; quando os campos
// forem adicionados eles passam a rodar de verdade. A verificação autoritativa
// (fonte) está em ../test_wasm_unload_callback_contract.py.

#include <gtest/gtest.h>
#include <cstring>
#include <type_traits>

#include "tab5_host_abi.h"
#include "tab5_wasm_runtime.h"

namespace {

template <typename T, typename = void> struct HasCallDepth : std::false_type {};
template <typename T> struct HasCallDepth<T, std::void_t<decltype(T::call_depth)>> : std::true_type {};

template <typename T, typename = void> struct HasInflightCalls : std::false_type {};
template <typename T> struct HasInflightCalls<T, std::void_t<decltype(T::inflight_calls)>> : std::true_type {};

template <typename T, typename = void> struct HasUnloadPending : std::false_type {};
template <typename T> struct HasUnloadPending<T, std::void_t<decltype(T::unload_pending)>> : std::true_type {};

template <typename Inst> void load_sample(Inst &inst)
{
    uint8_t dummy_wasm[] = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    tab5_app_context_t ctx = {};
    strncpy(ctx.app_id, "com.tab5.unloadcontract", sizeof(ctx.app_id) - 1);
    ASSERT_EQ(tab5_wasm_load_from_bytes(dummy_wasm, sizeof(dummy_wasm), 16384, 65536, &ctx, &inst), TAB5_OK);
    ASSERT_NE(inst.wasm_buf, nullptr);
}

template <typename Inst, typename DepthPtr, typename PendingPtr>
void check_unload_defers_while_call_active(Inst &inst, DepthPtr depth, PendingPtr pending)
{
    load_sample(inst);
    inst.*depth = 1; // uma chamada WAMR desta instância está ativa
    EXPECT_EQ(tab5_wasm_unload(&inst), TAB5_OK);
    EXPECT_NE(inst.wasm_buf, nullptr) << "unload durante callback não pode liberar o bytecode em execução";
    EXPECT_TRUE(inst.*pending) << "unload durante callback precisa marcar close pendente";
    EXPECT_EQ(inst.*depth, 1) << "unload não pode alterar a profundidade de chamada";
}

template <typename Inst, typename DepthPtr, typename PendingPtr>
void check_idle_processing_frees_sample(Inst &inst, DepthPtr depth, PendingPtr pending)
{
    load_sample(inst);
    inst.*depth = 1;
    ASSERT_EQ(tab5_wasm_unload(&inst), TAB5_OK); // defere
    ASSERT_NE(inst.wasm_buf, nullptr);

    inst.*depth = 0;      // a chamada encerrou (pós-join)
    inst.*pending = true; // encerramento permanece pendente do ponto de vista do runtime
    EXPECT_EQ(tab5_wasm_unload(&inst), TAB5_OK);
    EXPECT_EQ(inst.wasm_buf, nullptr) << "com a pilha vazia o encerramento pendente precisa liberar o bytecode";
    EXPECT_FALSE(inst.is_running);
}

template <typename Inst> void run_deferred_unload()
{
    Inst inst = {};
    if constexpr (HasCallDepth<Inst>::value && HasUnloadPending<Inst>::value) {
        check_unload_defers_while_call_active(inst, &Inst::call_depth, &Inst::unload_pending);
    } else if constexpr (HasInflightCalls<Inst>::value && HasUnloadPending<Inst>::value) {
        check_unload_defers_while_call_active(inst, &Inst::inflight_calls, &Inst::unload_pending);
    } else {
        GTEST_SKIP() << "campos de contrato (call_depth/inflight + unload_pending) ainda não "
                        "implementados na struct tab5_wasm_app_instance_t";
    }
}

template <typename Inst> void run_idle_processing()
{
    Inst inst = {};
    if constexpr (HasCallDepth<Inst>::value && HasUnloadPending<Inst>::value) {
        check_idle_processing_frees_sample(inst, &Inst::call_depth, &Inst::unload_pending);
    } else if constexpr (HasInflightCalls<Inst>::value && HasUnloadPending<Inst>::value) {
        check_idle_processing_frees_sample(inst, &Inst::inflight_calls, &Inst::unload_pending);
    } else {
        GTEST_SKIP() << "campos de contrato (call_depth/inflight + unload_pending) ainda não "
                        "implementados na struct tab5_wasm_app_instance_t";
    }
}

} // namespace

TEST(WasmUnloadContract, UnloadDuringActiveCallbackDefersTeardown)
{
    run_deferred_unload<tab5_wasm_app_instance_t>();
}

TEST(WasmUnloadContract, IdleCallStackProcessesPendingTeardown)
{
    run_idle_processing<tab5_wasm_app_instance_t>();
}
