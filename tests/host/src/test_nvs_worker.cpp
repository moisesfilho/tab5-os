/* Testes comportamentais do tab5_nvs_worker em host.
 *
 * O fonte de producao tab5_nvs_worker.cpp e compilado NESTE build com
 * -DESP_PLATFORM; as primitivas FreeRTOS que ele usa sao implementadas de
 * verdade por mocks/freertos_mock.cpp (std::thread + condition_variable), e o
 * NVS pelo mocks/nvs_mock.cpp (com stall one-shot e instrumentacao de
 * nvs_commit).  Nada aqui e "falso positivo": fila sincrona, timeout,
 * ABANDONED, copia de strings, commit so no worker e tasks estaticas sao
 * exercitados com comportamento real.
 *
 * Observacao sobre tempo: REQUEST_TIMEOUT_MS do worker e 2000 ms (constante
 * de producao), entao os testes de timeout gastam ~2 s cada.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#include "freertos_mock.hpp"
#include "nvs_mock.hpp"
#include "tab5_nvs_worker.h"

namespace {

constexpr uint32_t kWorkerStackWords = 2048; /* mesmo valor de producao */
constexpr uint64_t kWorkerTimeoutMarginMs = 300;

std::chrono::steady_clock::time_point now()
{
    return std::chrono::steady_clock::now();
}

uint64_t elapsed_ms(std::chrono::steady_clock::time_point from)
{
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(now() - from).count();
}

} // namespace

class NvsWorkerTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        hostmock::nvs_reset();
        hostmock::nvs_stall_clear();
    }

    void TearDown() override
    {
        hostmock::nvs_stall_clear();
    }
};

/* --- Task estatica e kernel -------------------------------------------------- */

TEST_F(NvsWorkerTest, WorkerCreatesStaticTaskWithInternalStackAndSingletonQueue)
{
    // Dispara a inicializacao lazy do worker.
    uint8_t v = 0;
    ASSERT_EQ(tab5_nvs_worker_get_u8("tab5", "probe", &v), TAB5_ERR_NOT_FOUND);

    EXPECT_TRUE(hostmock::freertos::has_task("tab5_nvs", kWorkerStackWords))
        << "a task do worker precisa ser criada com xTaskCreateStatic e stack "
           "de 2048 palavras (8 KiB)";
    EXPECT_TRUE(hostmock::freertos::has_single_slot_byte_queue())
        << "a fila sincrona precisa ter exatamente 1 slot de sizeof(uint8_t)";
    EXPECT_GE(hostmock::freertos::created_semaphore_count(), 3u);
    EXPECT_TRUE(hostmock::freertos::has_mutex_semaphore());
    EXPECT_GE(hostmock::freertos::created_task_count(), 1u);
}

/* --- Fila sincrona ------------------------------------------------------------ */

TEST_F(NvsWorkerTest, SetThenGetIsSynchronousRoundTrip)
{
    ASSERT_EQ(tab5_nvs_worker_set_u8("tab5", "brightness", 42), TAB5_OK);

    uint8_t v = 0;
    ASSERT_EQ(tab5_nvs_worker_get_u8("tab5", "brightness", &v), TAB5_OK);
    EXPECT_EQ(v, 42);

    // A persistencia ja esta efetivada quando a chamada sincrona retorna:
    // o dado esta no "flash" simulado (nvs_set_u8 + nvs_commit do worker).
    uint8_t stored = 0;
    ASSERT_TRUE(hostmock::nvs_read_u8("tab5", "brightness", &stored));
    EXPECT_EQ(stored, 42);
}

TEST_F(NvsWorkerTest, GetReadsDataSeededInNvs)
{
    hostmock::nvs_seed_u8("tab5", "files_hidden", 1);

    uint8_t v = 0;
    ASSERT_EQ(tab5_nvs_worker_get_u8("tab5", "files_hidden", &v), TAB5_OK);
    EXPECT_EQ(v, 1);
}

TEST_F(NvsWorkerTest, GetOnMissingNamespaceReturnsNotFound)
{
    uint8_t v = 99;
    EXPECT_EQ(tab5_nvs_worker_get_u8("tab5", "missing", &v), TAB5_ERR_NOT_FOUND);
    EXPECT_EQ(v, 99); /* out_val nao e tocado em erro */

    EXPECT_EQ(tab5_nvs_worker_get_u8("ns_inexistente", "k", &v), TAB5_ERR_NOT_FOUND);
}

TEST_F(NvsWorkerTest, NamespacePropagatesThroughWorker)
{
    ASSERT_EQ(tab5_nvs_worker_set_u8("radios", "wifi_en", 1), TAB5_OK);

    uint8_t v = 0;
    ASSERT_EQ(tab5_nvs_worker_get_u8("radios", "wifi_en", &v), TAB5_OK);
    EXPECT_EQ(v, 1);

    // O mesmo nome de chave em outro namespace nao vaza.
    EXPECT_EQ(tab5_nvs_worker_get_u8("tab5", "wifi_en", &v), TAB5_ERR_NOT_FOUND);
}

TEST_F(NvsWorkerTest, InvalidArgumentsAreRejected)
{
    uint8_t v = 0;
    EXPECT_EQ(tab5_nvs_worker_get_u8(nullptr, "key", &v), TAB5_ERR_INVALID_ARG);
    EXPECT_EQ(tab5_nvs_worker_get_u8("tab5", nullptr, &v), TAB5_ERR_INVALID_ARG);
    EXPECT_EQ(tab5_nvs_worker_get_u8("tab5", "key", nullptr), TAB5_ERR_INVALID_ARG);

    EXPECT_EQ(tab5_nvs_worker_set_u8(nullptr, "key", 1), TAB5_ERR_INVALID_ARG);
    EXPECT_EQ(tab5_nvs_worker_set_u8("tab5", nullptr, 1), TAB5_ERR_INVALID_ARG);
}

/* --- Copia de strings ---------------------------------------------------------- */

TEST_F(NvsWorkerTest, NamespaceAndKeyAtMaxLengthAreAccepted)
{
    // NVS limita ns/key a 15 chars + NUL; o worker copia para buffers de 16.
    const char ns15[] = "abcdefghijklmno";  /* 15 chars */
    const char key15[] = "k12345678901234"; /* 15 chars */

    ASSERT_EQ(tab5_nvs_worker_set_u8(ns15, key15, 7), TAB5_OK);

    uint8_t v = 0;
    ASSERT_EQ(tab5_nvs_worker_get_u8(ns15, key15, &v), TAB5_OK);
    EXPECT_EQ(v, 7);
}

TEST_F(NvsWorkerTest, OverlongNamespaceAndKeyAreRejected)
{
    const char ns16[] = "abcdefghijklmnop";  /* 16 chars */
    const char key16[] = "k123456789012345"; /* 16 chars */

    uint8_t v = 0;
    EXPECT_EQ(tab5_nvs_worker_set_u8(ns16, "key", 1), TAB5_ERR_INVALID_ARG);
    EXPECT_EQ(tab5_nvs_worker_set_u8("tab5", key16, 1), TAB5_ERR_INVALID_ARG);
    EXPECT_EQ(tab5_nvs_worker_get_u8(ns16, "key", &v), TAB5_ERR_INVALID_ARG);
    EXPECT_EQ(tab5_nvs_worker_get_u8("tab5", key16, &v), TAB5_ERR_INVALID_ARG);
}

TEST_F(NvsWorkerTest, SubmitCopiesCallerStringsBeforeReturning)
{
    char ns[32];
    char key[32];
    std::strcpy(ns, "tab5");
    std::strcpy(key, "origem");

    ASSERT_EQ(tab5_nvs_worker_set_u8(ns, key, 5), TAB5_OK);

    // Corrompe os buffers do chamador: a requisicao ja copiou os bytes.
    std::memset(ns, 'X', sizeof(ns));
    std::memset(key, 'Y', sizeof(key));

    uint8_t v = 0;
    ASSERT_EQ(tab5_nvs_worker_get_u8("tab5", "origem", &v), TAB5_OK);
    EXPECT_EQ(v, 5);
}

TEST_F(NvsWorkerTest, RequestsDoNotLeakStateBetweenKeys)
{
    // Chave longa primeiro; o slot do Request e reutilizado depois por uma
    // chave curta e por um valor 0 — nenhum byte residual pode vazar.
    const char key15[] = "k12345678901234";
    ASSERT_EQ(tab5_nvs_worker_set_u8("tab5", key15, 77), TAB5_OK);

    uint8_t v = 0;
    ASSERT_EQ(tab5_nvs_worker_get_u8("tab5", key15, &v), TAB5_OK);
    EXPECT_EQ(v, 77);

    // Chave de 1 char depois da longa: strncpy preenche com NUL e a
    // operacao nova nao herda nada do request anterior.
    ASSERT_EQ(tab5_nvs_worker_set_u8("tab5", "x", 3), TAB5_OK);
    ASSERT_EQ(tab5_nvs_worker_get_u8("tab5", "x", &v), TAB5_OK);
    EXPECT_EQ(v, 3);

    // A chave longa continua intacta no store (nao foi "lida" como a curta).
    ASSERT_EQ(tab5_nvs_worker_get_u8("tab5", key15, &v), TAB5_OK);
    EXPECT_EQ(v, 77);

    // s_request.result_value e zerado entre requisicoes: ler uma chave com
    // valor 0 deve devolver 0, nunca o residual 77/3 do slot anterior.
    ASSERT_EQ(tab5_nvs_worker_set_u8("tab5", "zero", 0), TAB5_OK);
    ASSERT_EQ(tab5_nvs_worker_get_u8("tab5", "zero", &v), TAB5_OK);
    EXPECT_EQ(v, 0);
}

/* --- nvs_commit somente no worker ----------------------------------------------- */

TEST_F(NvsWorkerTest, CommitRunsExactlyOncePerSetOnWorkerThread)
{
    const uint64_t before = hostmock::nvs_commit_call_count();
    const std::thread::id caller = std::this_thread::get_id();

    ASSERT_EQ(tab5_nvs_worker_set_u8("tab5", "vol", 5), TAB5_OK);

    EXPECT_EQ(hostmock::nvs_commit_call_count(), before + 1)
        << "um set_u8 -> exatamente um nvs_commit (a chamada so retorna "
           "depois do commit)";
    EXPECT_NE(hostmock::nvs_last_commit_thread(), caller)
        << "o nvs_commit precisa rodar na task do worker, nunca na thread "
           "do chamador (WASM)";
}

TEST_F(NvsWorkerTest, ReadPathNeverCommits)
{
    const uint64_t before = hostmock::nvs_commit_call_count();

    uint8_t v = 0;
    EXPECT_EQ(tab5_nvs_worker_get_u8("tab5", "k", &v), TAB5_ERR_NOT_FOUND);

    EXPECT_EQ(hostmock::nvs_commit_call_count(), before) << "leitura NVS nao pode chamar nvs_commit";
}

/* --- Timeout / ABANDONED --------------------------------------------------------- */

TEST_F(NvsWorkerTest, UnresponsiveNvsMakesSubmitTimeOutAndWorkerRecovers)
{
    // O worker fica preso 2.6 s dentro de nvs_open (stall one-shot do mock);
    // o submit espera REQUEST_TIMEOUT_MS (2 s) pelo s_done e abandona.
    hostmock::nvs_stall_next_open_once(2600);
    const auto t0 = now();

    EXPECT_EQ(tab5_nvs_worker_set_u8("tab5", "slow", 1), TAB5_ERR_TIMEOUT);

    const auto took = elapsed_ms(t0);
    EXPECT_GE(took, 2000);
    EXPECT_LE(took, 2600 + kWorkerTimeoutMarginMs);

    // O worker termina o request abandonado (libera o slot) e segue vivo:
    // o proximo request da fila sincrona funciona e le o valor que o set
    // abandonado acabou persistindo quando o stall expirou.
    uint8_t v = 0;
    ASSERT_EQ(tab5_nvs_worker_get_u8("tab5", "slow", &v), TAB5_OK);
    EXPECT_EQ(v, 1);
}

TEST_F(NvsWorkerTest, ConcurrentSubmitTimesOutWhileWorkerIsBusy)
{
    // 1a requisicao, em outra thread, fica presa no stall (2.6 s) e segura o
    // semaforo "available". Uma 2a requisicao concorrente espera o slot por
    // REQUEST_TIMEOUT_MS e estoura no proprio portao (xSemaphoreTake de
    // s_available), sem tocar no Request em voo.
    hostmock::nvs_stall_next_open_once(2600);

    std::thread first_submit([] { EXPECT_EQ(tab5_nvs_worker_set_u8("tab5", "slow", 9), TAB5_ERR_TIMEOUT); });

    // Espera o worker consumir o stall: a partir daqui ele esta preso em
    // nvs_open e o semaforo "available" esta com o first_submit.
    ASSERT_TRUE(hostmock::nvs_wait_stall_consumed(3000)) << "o worker nao consumiu o stall de nvs_open a tempo";

    // O portao do semaforo "available" estoura em ~2 s (worker ainda preso).
    const auto t0 = now();
    EXPECT_EQ(tab5_nvs_worker_set_u8("tab5", "other", 1), TAB5_ERR_TIMEOUT);
    const auto took = elapsed_ms(t0);
    EXPECT_GE(took, 2000);
    EXPECT_LE(took, 2000 + kWorkerTimeoutMarginMs + 500);

    first_submit.join();

    // Recuperacao: quando o worker conclui o request abandonado, ele libera
    // o slot e a fila volta a funcionar.
    uint8_t v = 0;
    ASSERT_EQ(tab5_nvs_worker_get_u8("tab5", "slow", &v), TAB5_OK);
    EXPECT_EQ(v, 9);
}
