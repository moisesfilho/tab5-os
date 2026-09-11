// Testes host (gtest, sem goldens) para a LÓGICA PURA de dispatch do
// "Boneco de Lata" (Serial Automation Bridge) — PLANO boneco-de-lata.md, §2.
//
// O que é coberto aqui:
//   * O envelope NDJSON de resposta do plano (§2.1):
//       sucesso: {"status":"ok","action":"<ação>","data":{...}}
//       erro:    {"status":"error","action":"<ação>","error":"<mensagem>"}
//   * A validação de entrada: JSON malformado, linha vazia, comando
//     desconhecido e parâmetros ausentes/ilegais produzem status error.
//   * Os handlers rastreados do §2.2: app.list/open/close/active,
//     ui.click/tap/type, server.start/stop/status, screen.shot,
//     screen.dump, sys.info.
//   * O protocolo de streaming do screen.dump (§2.2 item 4): frame start
//     com size/chunks, frames chunk com índice e base64, frame end; quando
//     o dispatch devolve o stream completo num buffer único, o teste
//     reconstrói o arquivo e o compara byte a byte com o original.
//
// Estado pré-implementação (TDD vermelho): enquanto components/os/core/
// serial_bridge.{h,cpp} não existirem, a suíte inteira é SKIP — esperado e
// documentado como "falha esperada pela ausência da implementação". Quando o
// módulo for criado e adicionado ao CMake (ife(EXISTS ...)), a suíte passa a
// compilar e exercitar o contrato inteiro automaticamente, sem alterar este
// arquivo.
//
// Contrato fixado por esta suíte (o developer implementa contra ele; se a API
// real divergir, é preciso justificar a mudança do teste):
//   extern "C" {
//       // 1 linha NDJSON de comando -> respostas NDJSON (0+ linhas) em out.
//       // Retorna nº de bytes escritos (sem o NUL final) ou <0 em falha
//       // (ex.: buffer pequeno demais). out nunca é escrito além de out_sz.
//       int serial_bridge_dispatch(const char *json_line, char *out, size_t out_sz);
//   }

#include <gtest/gtest.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

#if __has_include("serial_bridge.h")
#include "serial_bridge.h"
#define SERIAL_BRIDGE_HEADER_AVAILABLE 1
#else
#define SERIAL_BRIDGE_HEADER_AVAILABLE 0
#endif

#if SERIAL_BRIDGE_HEADER_AVAILABLE && __has_include("screenshot.h")
#include "screenshot.h"
#define SCREENSHOT_HEADER_AVAILABLE 1
#else
#define SCREENSHOT_HEADER_AVAILABLE 0
#endif

extern "C" {
#include "cJSON.h"
}

#include "path_redirect.hpp"

namespace {

#if SERIAL_BRIDGE_HEADER_AVAILABLE
constexpr size_t kDispatchBufferSize = 4u * 1024u * 1024u;

// ---------------------------------------------------------------------------
// Helpers de teste (independem da implementação; compilam apenas quando o
// header do bridge existir — caso contrário seriam `unused-function`).
// ---------------------------------------------------------------------------

struct DispatchResult {
    int rc = -1;
    std::string text; /* rc bytes (o dispatch pode não NUL-terminar) */
};

DispatchResult run_dispatch(const std::string &cmd)
{
    std::vector<char> out(kDispatchBufferSize);
    const int rc = serial_bridge_dispatch(cmd.c_str(), out.data(), out.size());
    DispatchResult r;
    r.rc = rc;
    if (rc > 0) {
        r.text.assign(out.data(), static_cast<size_t>(rc));
    }
    return r;
}

// Divide a saída em frames NDJSON (0+ linhas separadas por '\n').
std::vector<std::string> split_frames(const std::string &text)
{
    std::vector<std::string> frames;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = (nl == std::string::npos) ? text.substr(pos) : text.substr(pos, nl - pos);
        /* trims de espaços laterais */
        size_t b = line.find_first_not_of(" \t\r");
        size_t e = line.find_last_not_of(" \t\r");
        if (b != std::string::npos && e != std::string::npos) {
            frames.push_back(line.substr(b, e - b + 1));
        }
        if (nl == std::string::npos) {
            break;
        }
        pos = nl + 1;
    }
    return frames;
}

struct JsonDoc {
    cJSON *root = nullptr;
    explicit JsonDoc(const std::string &line)
    {
        root = cJSON_Parse(line.c_str());
    }
    ~JsonDoc()
    {
        if (root != nullptr) {
            cJSON_Delete(root);
        }
    }
    JsonDoc(const JsonDoc &) = delete;
    JsonDoc &operator=(const JsonDoc &) = delete;
};

const cJSON *field(const cJSON *obj, const char *name)
{
    if (obj == nullptr) {
        return nullptr;
    }
    return cJSON_GetObjectItemCaseSensitive(obj, name);
}

std::string field_str(const cJSON *obj, const char *name)
{
    const cJSON *v = field(obj, name);
    return (v != nullptr && cJSON_IsString(v) && v->valuestring != nullptr) ? v->valuestring : "";
}

// Valida o envelope de erro do plano e devolve a mensagem.
std::string expect_error_frame(const std::string &frame)
{
    JsonDoc doc(frame);
    EXPECT_NE(doc.root, nullptr) << "resposta de erro precisa ser JSON válido: " << frame;
    if (doc.root == nullptr) {
        return "";
    }
    EXPECT_STREQ(field_str(doc.root, "status").c_str(), "error") << "status deve ser \"error\": " << frame;
    const cJSON *action = field(doc.root, "action");
    const cJSON *error = field(doc.root, "error");
    EXPECT_TRUE(action != nullptr) << "envelope de erro precisa ter o campo \"action\": " << frame;
    EXPECT_TRUE(error != nullptr && cJSON_IsString(error) && error->valuestring != nullptr &&
                strlen(error->valuestring) > 0)
        << "envelope de erro precisa ter o campo \"error\" não vazio: " << frame;
    return field_str(doc.root, "error");
}

std::string expect_ok_frame(const std::string &frame, const std::string &action_name)
{
    JsonDoc doc(frame);
    EXPECT_NE(doc.root, nullptr) << "resposta ok precisa ser JSON válido: " << frame;
    if (doc.root == nullptr) {
        return "";
    }
    EXPECT_STREQ(field_str(doc.root, "status").c_str(), "ok") << "status deve ser \"ok\": " << frame;
    if (!action_name.empty()) {
        EXPECT_STREQ(field_str(doc.root, "action").c_str(), action_name.c_str())
            << "action deve ecoar o nome do comando: " << frame;
    }
    return field_str(doc.root, "action");
}

// Base64 decodifica (RFC 4648) para a reconstrução byte-idêntica.
std::string base64_decode(const std::string &in)
{
    static const char kTab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static int kRev[256] = {0};
    static bool kInit = false;
    if (!kInit) {
        for (int i = 0; i < 256; ++i) {
            kRev[i] = -1;
        }
        for (int i = 0; i < 64; ++i) {
            kRev[static_cast<unsigned char>(kTab[i])] = i;
        }
        kInit = true;
    }
    std::string out;
    out.reserve((in.size() / 4) * 3);
    int acc = 0, nbits = 0;
    for (unsigned char c : in) {
        if (c == '=' || kRev[c] < 0) {
            continue;
        }
        acc = (acc << 6) | kRev[c];
        nbits += 6;
        if (nbits >= 8) {
            nbits -= 8;
            out.push_back(static_cast<char>((acc >> nbits) & 0xFF));
        }
    }
    return out;
}

// Cria um "BMP" virtual com conteúdo determinístico e devolve os bytes.
std::string create_screenshot_fixture()
{
    std::string payload;
    payload.reserve(4096);
    /* "BMP" (0x42 0x4D) + padding + padrão determinístico não-trivial */
    payload.append("\x42\x4D", 2);
    for (int i = 0; i < 14; ++i) {
        payload.push_back(static_cast<char>(0x00));
    }
    for (int i = 0; i < 4096 - 16; ++i) {
        payload.push_back(static_cast<char>((i * 7 + 3) & 0xFF));
    }
    return payload;
}

#endif /* SERIAL_BRIDGE_HEADER_AVAILABLE */

} // namespace

// ---------------------------------------------------------------------------
// Fase vermelha: módulo ainda não implementado.
// ---------------------------------------------------------------------------

#if SERIAL_BRIDGE_HEADER_AVAILABLE == 0

TEST(SerialBridgeDispatchContract, ImplementationPendingProducesExpectedSkip)
{
    GTEST_SKIP() << "FALHA ESPERADA (pré-implementação): components/os/core/"
                    "serial_bridge.h ainda não existe. Quando o módulo for "
                    "criado (PLANO boneco-de-lata.md Passo 1) e serial_bridge.cpp "
                    "for adicionado ao CMake, esta suíte compila e exercita todo "
                    "o contrato JSON->JSON. Nenhum código de produção foi alterado.";
}

#else

// ---------------------------------------------------------------------------
// Estados do screen.shot (revisão final: busy, erro, timeout e sucesso).
// ---------------------------------------------------------------------------
// As definições fracas em mocks/serial_bridge_subsystem_mocks.cpp fixam
// screenshot_take()/wait/get_last_path num único comportamento (OK/OK/path
// fixo), o que não permite distinguir os estados da API. Este TU sobrepõe
// essas definições com versões fortes de linkage "C" cujo retorno é
// controlado por variáveis locais — produção não muda (screenshot.cpp não é
// compilado no build host; se algum dia for, as fracas do mock cedem e este
// contrato passa a exercitar o firmware real).
#if SCREENSHOT_HEADER_AVAILABLE

namespace screenshot_ctl {

screenshot_result_t g_take_result = SCREENSHOT_RESULT_OK;
screenshot_result_t g_wait_result = SCREENSHOT_RESULT_OK;
int g_take_calls = 0;
int g_wait_calls = 0;
uint32_t g_wait_timeout_ms = 0;
const char *g_last_path = "/sdcard/screenshots/print_latest.bmp";

void reset()
{
    g_take_result = SCREENSHOT_RESULT_OK;
    g_wait_result = SCREENSHOT_RESULT_OK;
    g_take_calls = 0;
    g_wait_calls = 0;
    g_wait_timeout_ms = 0;
    g_last_path = "/sdcard/screenshots/print_latest.bmp";
}

} // namespace screenshot_ctl

/* Escopo global (fora do anonymous namespace): símbolos de linkage "C" com
 * definição forte para vencer as fracas do mock no link. */
extern "C" {

screenshot_result_t screenshot_take(void)
{
    ++screenshot_ctl::g_take_calls;
    return screenshot_ctl::g_take_result;
}

screenshot_result_t screenshot_wait_for_completion(uint32_t timeout_ms)
{
    ++screenshot_ctl::g_wait_calls;
    screenshot_ctl::g_wait_timeout_ms = timeout_ms;
    return screenshot_ctl::g_wait_result;
}

const char *screenshot_get_last_path(void)
{
    return screenshot_ctl::g_last_path;
}

} // extern "C"

namespace {

class ScreenShotStateContract : public ::testing::Test {
  protected:
    void SetUp() override
    {
        screenshot_ctl::reset();
    }
    void TearDown() override
    {
        screenshot_ctl::reset();
    }
};

// SUCESSO: take=OK + wait=OK -> envelope ok com data.path ecoando o getter;
// o dispatch precisa conectar take -> wait(prazo 5000 ms) -> getter.
TEST_F(ScreenShotStateContract, SuccessEchoesLastPathAndWiresTakeWait)
{
    screenshot_ctl::g_last_path = "/sdcard/screenshots/print_ok.bmp";

    const auto r = run_dispatch("{\"cmd\":\"screen.shot\"}");
    ASSERT_GT(r.rc, 0);
    const auto frames = split_frames(r.text);
    ASSERT_EQ(frames.size(), 1u);
    expect_ok_frame(frames[0], "screen.shot");

    JsonDoc doc(frames[0]);
    ASSERT_NE(doc.root, nullptr);
    const cJSON *data = field(doc.root, "data");
    ASSERT_NE(data, nullptr);
    ASSERT_TRUE(cJSON_IsString(field(data, "path")));
    EXPECT_STREQ(field(data, "path")->valuestring, "/sdcard/screenshots/print_ok.bmp")
        << "ok precisa ecoar o último path do getter";

    EXPECT_EQ(screenshot_ctl::g_take_calls, 1) << "screen.shot precisa invocar screenshot_take uma vez";
    EXPECT_EQ(screenshot_ctl::g_wait_calls, 1) << "take=OK precisa ser seguido de wait_for_completion";
    EXPECT_EQ(screenshot_ctl::g_wait_timeout_ms, 5000u)
        << "wait precisa usar o prazo de 5000 ms aplicado pelo firmware";
}

// BUSY: take=BUSY -> error "screenshot ja esta em andamento" sem avançar para
// a espera (contrato: estado ocupado aborta o fluxo imediatamente).
TEST_F(ScreenShotStateContract, BusyYieldsImmediateErrorWithoutWaiting)
{
    screenshot_ctl::g_take_result = SCREENSHOT_RESULT_BUSY;

    const auto r = run_dispatch("{\"cmd\":\"screen.shot\"}");
    ASSERT_GT(r.rc, 0);
    const auto frames = split_frames(r.text);
    ASSERT_EQ(frames.size(), 1u);
    const std::string msg = expect_error_frame(frames[0]);
    EXPECT_NE(msg.find("em andamento"), std::string::npos)
        << "busy precisa ser reportado como screenshot em andamento: " << msg;
    EXPECT_EQ(screenshot_ctl::g_wait_calls, 0) << "take=BUSY precisa abortar antes de esperar a conclusão";
}

// ERRO na captura: take=ERROR (e o TIMEOUT que take jamais deveria devolver,
// mas que o else-if do dispatch cobre) -> error "falha ao iniciar screenshot".
TEST_F(ScreenShotStateContract, CaptureErrorYieldsStartupErrorWithoutWaiting)
{
    for (const screenshot_result_t bad_take : {SCREENSHOT_RESULT_ERROR, SCREENSHOT_RESULT_TIMEOUT}) {
        screenshot_ctl::reset();
        screenshot_ctl::g_take_result = bad_take;

        const auto r = run_dispatch("{\"cmd\":\"screen.shot\"}");
        ASSERT_GT(r.rc, 0);
        const auto frames = split_frames(r.text);
        ASSERT_EQ(frames.size(), 1u);
        const std::string msg = expect_error_frame(frames[0]);
        EXPECT_NE(msg.find("iniciar screenshot"), std::string::npos)
            << "falha no take precisa ser reportada como falha de início: " << msg;
        EXPECT_EQ(screenshot_ctl::g_wait_calls, 0) << "take!=OK precisa abortar antes de esperar a conclusão";
    }
}

// TIMEOUT da espera: take=OK + wait=TIMEOUT -> error "screenshot falhou ou
// nao terminou no prazo", com wait efetivamente chamado com 5000 ms.
TEST_F(ScreenShotStateContract, CompletionTimeoutYieldsFailureError)
{
    screenshot_ctl::g_wait_result = SCREENSHOT_RESULT_TIMEOUT;

    const auto r = run_dispatch("{\"cmd\":\"screen.shot\"}");
    ASSERT_GT(r.rc, 0);
    const auto frames = split_frames(r.text);
    ASSERT_EQ(frames.size(), 1u);
    const std::string msg = expect_error_frame(frames[0]);
    EXPECT_NE(msg.find("nao terminou no prazo"), std::string::npos)
        << "timeout de conclusão precisa ser reportado: " << msg;
    EXPECT_EQ(screenshot_ctl::g_wait_calls, 1);
    EXPECT_EQ(screenshot_ctl::g_wait_timeout_ms, 5000u);
}

// ERRO na conclusão/gravação: take=OK + wait=ERROR -> mesmo verbo de falha
// mas com wait chamado (a espera ocorreu e a gravação falhou).
TEST_F(ScreenShotStateContract, CompletionErrorYieldsFailureError)
{
    screenshot_ctl::g_wait_result = SCREENSHOT_RESULT_ERROR;

    const auto r = run_dispatch("{\"cmd\":\"screen.shot\"}");
    ASSERT_GT(r.rc, 0);
    const auto frames = split_frames(r.text);
    ASSERT_EQ(frames.size(), 1u);
    const std::string msg = expect_error_frame(frames[0]);
    EXPECT_NE(msg.find("nao terminou no prazo"), std::string::npos)
        << "falha de gravação/conclusão precisa ser reportada: " << msg;
    EXPECT_EQ(screenshot_ctl::g_wait_calls, 1);
}

} // namespace

#endif /* SCREENSHOT_HEADER_AVAILABLE */

// ---------------------------------------------------------------------------
// Envelope e validação de entrada (PLANO §2.1)
// ---------------------------------------------------------------------------

TEST(SerialBridgeDispatchContract, SuccessEnvelope)
{
    const auto r = run_dispatch("{\"cmd\":\"app.list\"}");
    ASSERT_GT(r.rc, 0) << "dispatch deve escrever a resposta (rc>0)";
    const auto frames = split_frames(r.text);
    ASSERT_EQ(frames.size(), 1u);
    const std::string action = expect_ok_frame(frames[0], "app.list");
    ASSERT_STREQ(action.c_str(), "app.list");
    JsonDoc doc(frames[0]);
    ASSERT_NE(doc.root, nullptr);
    EXPECT_NE(field(doc.root, "data"), nullptr) << "ok precisa carregar \"data\": " << frames[0];
}

TEST(SerialBridgeDispatchContract, MalformedJsonYieldsErrorEnvelope)
{
    const auto r = run_dispatch("isto não é json {");
    ASSERT_GT(r.rc, 0);
    const auto frames = split_frames(r.text);
    ASSERT_GE(frames.size(), 1u);
    const std::string msg = expect_error_frame(frames[0]);
    EXPECT_FALSE(msg.empty());
}

TEST(SerialBridgeDispatchContract, EmptyAndWhitespaceLinesAreRejected)
{
    for (const std::string &cmd : {std::string(""), std::string("   \t \n"), std::string("\n")}) {
        const auto r = run_dispatch(cmd);
        ASSERT_GT(r.rc, 0);
        const auto frames = split_frames(r.text);
        ASSERT_GE(frames.size(), 1u);
        expect_error_frame(frames[0]);
    }
}

TEST(SerialBridgeDispatchContract, UnknownCommandYieldsErrorWithCommandName)
{
    const auto r = run_dispatch("{\"cmd\":\"app.frobnicate\"}");
    ASSERT_GT(r.rc, 0);
    const auto frames = split_frames(r.text);
    ASSERT_GE(frames.size(), 1u);
    JsonDoc doc(frames[0]);
    ASSERT_NE(doc.root, nullptr);
    const std::string action = field_str(doc.root, "action");
    const cJSON *error = field(doc.root, "error");
    const std::string msg = (error != nullptr && cJSON_IsString(error) && error->valuestring) ? error->valuestring : "";
    EXPECT_FALSE(msg.empty());
    /* O comando desconhecido precisa aparecer na resposta (action ou error) */
    EXPECT_TRUE(action == "app.frobnicate" || msg.find("app.frobnicate") != std::string::npos)
        << "comando desconhecido precisa ser identificado na resposta: " << frames[0];
}

TEST(SerialBridgeDispatchContract, DispatchNeverOverflowsSmallBuffer)
{
    std::vector<char> tiny(8);
    if constexpr (SERIAL_BRIDGE_HEADER_AVAILABLE) {
        const int rc = serial_bridge_dispatch("{\"cmd\":\"app.list\"}", tiny.data(), tiny.size());
        /* Sem ASAN/fortify isto não detecta corrupção, mas fixa o contrato:
         * ou retorna negativo, ou devolve no máximo out_sz bytes. */
        EXPECT_TRUE(rc < 0 || rc <= static_cast<int>(tiny.size()));
    }
}

// ---------------------------------------------------------------------------
// Gestão de aplicações (PLANO §2.2 item 1)
// ---------------------------------------------------------------------------

TEST(SerialBridgeDispatchContract, AppOpenRequiresIdAndAcksRoundtrip)
{
    const auto missing = run_dispatch("{\"cmd\":\"app.open\"}");
    ASSERT_GT(missing.rc, 0);
    expect_error_frame(split_frames(missing.text)[0]);

    const auto ok = run_dispatch("{\"cmd\":\"app.open\",\"id\":\"com.tab5.terminal\"}");
    ASSERT_GT(ok.rc, 0);
    const auto frames = split_frames(ok.text);
    ASSERT_EQ(frames.size(), 1u);
    expect_ok_frame(frames[0], "app.open");
    JsonDoc doc(frames[0]);
    ASSERT_NE(doc.root, nullptr);
    const cJSON *data = field(doc.root, "data");
    ASSERT_NE(data, nullptr);
    EXPECT_STREQ(field_str(data, "id").c_str(), "com.tab5.terminal");
}

TEST(SerialBridgeDispatchContract, AppCloseAndAppActiveRespondOk)
{
    const auto close = run_dispatch("{\"cmd\":\"app.close\"}");
    ASSERT_GT(close.rc, 0);
    expect_ok_frame(split_frames(close.text)[0], "app.close");

    const auto active = run_dispatch("{\"cmd\":\"app.active\"}");
    ASSERT_GT(active.rc, 0);
    const auto frames = split_frames(active.text);
    ASSERT_EQ(frames.size(), 1u);
    expect_ok_frame(frames[0], "app.active");
}

// ---------------------------------------------------------------------------
// Automação de interface (PLANO §2.2 item 2)
// ---------------------------------------------------------------------------

TEST(SerialBridgeDispatchContract, UiClickValidatesXAndYAsIntegers)
{
    const auto ok = run_dispatch("{\"cmd\":\"ui.click\",\"x\":640,\"y\":400}");
    ASSERT_GT(ok.rc, 0);
    const auto ok_frames = split_frames(ok.text);
    ASSERT_EQ(ok_frames.size(), 1u);
    expect_ok_frame(ok_frames[0], "ui.click");
    JsonDoc doc(ok_frames[0]);
    ASSERT_NE(doc.root, nullptr);
    const cJSON *data = field(doc.root, "data");
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(cJSON_IsNumber(field(data, "x"))) << "x precisa ser ecoado numérico";
    EXPECT_TRUE(cJSON_IsNumber(field(data, "y"))) << "y precisa ser ecoado numérico";

    /* coordenadas ausentes */
    const auto missing = run_dispatch("{\"cmd\":\"ui.click\",\"x\":100}");
    ASSERT_GT(missing.rc, 0);
    expect_error_frame(split_frames(missing.text)[0]);

    const auto missing_all = run_dispatch("{\"cmd\":\"ui.click\"}");
    ASSERT_GT(missing_all.rc, 0);
    expect_error_frame(split_frames(missing_all.text)[0]);

    /* coordenada não numérica */
    const auto bad = run_dispatch("{\"cmd\":\"ui.click\",\"x\":\"centro\",\"y\":10}");
    ASSERT_GT(bad.rc, 0);
    expect_error_frame(split_frames(bad.text)[0]);
}

TEST(SerialBridgeDispatchContract, UiTapRequiresTargetOrSymbol)
{
    const auto by_target = run_dispatch("{\"cmd\":\"ui.tap\",\"target\":\"Conectar\"}");
    ASSERT_GT(by_target.rc, 0);
    expect_ok_frame(split_frames(by_target.text)[0], "ui.tap");

    const auto by_symbol = run_dispatch("{\"cmd\":\"ui.tap\",\"symbol\":\"CLOSE\"}");
    ASSERT_GT(by_symbol.rc, 0);
    expect_ok_frame(split_frames(by_symbol.text)[0], "ui.tap");

    const auto neither = run_dispatch("{\"cmd\":\"ui.tap\"}");
    ASSERT_GT(neither.rc, 0);
    expect_error_frame(split_frames(neither.text)[0]);
}

TEST(SerialBridgeDispatchContract, UiTypeRequiresTextAndReportsCharCount)
{
    const auto ok = run_dispatch("{\"cmd\":\"ui.type\",\"text\":\"ls -la\\n\"}");
    ASSERT_GT(ok.rc, 0);
    const auto frames = split_frames(ok.text);
    ASSERT_EQ(frames.size(), 1u);
    expect_ok_frame(frames[0], "ui.type");
    JsonDoc doc(frames[0]);
    ASSERT_NE(doc.root, nullptr);
    const cJSON *data = field(doc.root, "data");
    ASSERT_NE(data, nullptr);
    const cJSON *chars = field(data, "chars");
    EXPECT_TRUE(cJSON_IsNumber(chars)) << "ui.type precisa reportar chars numérico";

    const auto missing = run_dispatch("{\"cmd\":\"ui.type\"}");
    ASSERT_GT(missing.rc, 0);
    expect_error_frame(split_frames(missing.text)[0]);
}

// ---------------------------------------------------------------------------
// Servidor de arquivos (PLANO §2.2 item 3)
// ---------------------------------------------------------------------------

TEST(SerialBridgeDispatchContract, ServerStartReportsRunningAndUrl)
{
    const auto r = run_dispatch("{\"cmd\":\"server.start\"}");
    ASSERT_GT(r.rc, 0);
    const auto frames = split_frames(r.text);
    ASSERT_EQ(frames.size(), 1u);
    expect_ok_frame(frames[0], "server.start");
    JsonDoc doc(frames[0]);
    ASSERT_NE(doc.root, nullptr);
    const cJSON *data = field(doc.root, "data");
    ASSERT_NE(data, nullptr);
    const cJSON *running = field(data, "running");
    EXPECT_TRUE(cJSON_IsBool(running)) << "server.start precisa reportar running booleano";
    EXPECT_TRUE(field(data, "url") != nullptr || field(data, "ip") != nullptr)
        << "server.start precisa carregar url ou ip";
}

TEST(SerialBridgeDispatchContract, ServerStopAndStatusRespondOk)
{
    const auto stop = run_dispatch("{\"cmd\":\"server.stop\"}");
    ASSERT_GT(stop.rc, 0);
    expect_ok_frame(split_frames(stop.text)[0], "server.stop");

    const auto status = run_dispatch("{\"cmd\":\"server.status\"}");
    ASSERT_GT(status.rc, 0);
    const auto frames = split_frames(status.text);
    ASSERT_EQ(frames.size(), 1u);
    expect_ok_frame(frames[0], "server.status");
}

// ---------------------------------------------------------------------------
// Captura de tela (PLANO §2.2 item 4)
// ---------------------------------------------------------------------------

TEST(SerialBridgeDispatchContract, ScreenShotReportsPath)
{
    const auto r = run_dispatch("{\"cmd\":\"screen.shot\"}");
    ASSERT_GT(r.rc, 0);
    const auto frames = split_frames(r.text);
    ASSERT_EQ(frames.size(), 1u);
    expect_ok_frame(frames[0], "screen.shot");
    JsonDoc doc(frames[0]);
    ASSERT_NE(doc.root, nullptr);
    const cJSON *data = field(doc.root, "data");
    ASSERT_NE(data, nullptr);
    const cJSON *path = field(data, "path");
    EXPECT_TRUE(cJSON_IsString(path) && path->valuestring != nullptr && strlen(path->valuestring) > 0)
        << "screen.shot precisa reportar path do screenshot";
}

TEST(SerialBridgeDispatchContract, ScreenDumpStartFrameAndByteIdenticalReconstruction)
{
    /* 0) provisiona o diretório de screenshots (o redirect do host não o cria) */
    const int mk = ::mkdir("/sdcard/screenshots", 0755);
    ASSERT_TRUE(mk == 0 || errno == EEXIST) << "não foi possível criar /sdcard/screenshots";

    /* 1) fixture "BMP" virtual: escrita via *fopen* (o --wrap redireciona) */
    const char *virt = "/sdcard/screenshots/print_contract.bmp";
    const std::string payload = create_screenshot_fixture();
    FILE *f = fopen(virt, "wb");
    ASSERT_NE(f, nullptr) << "path_redirect precisa permitir criar a fixture";
    ASSERT_EQ(fwrite(payload.data(), 1, payload.size(), f), payload.size());
    fclose(f);

    /* 2) dispositiva o dump do arquivo */
    const std::string cmd = std::string("{\"cmd\":\"screen.dump\",\"path\":\"") + virt + "\"}";
    const auto r = run_dispatch(cmd);
    ASSERT_GT(r.rc, 0);

    const auto frames = split_frames(r.text);
    ASSERT_GE(frames.size(), 1u) << "screen.dump precisa emitir ao menos o frame start";

    /* 3) frame start: status/action/event=start com size e chunks */
    JsonDoc start_doc(frames[0]);
    ASSERT_NE(start_doc.root, nullptr);
    EXPECT_STREQ(field_str(start_doc.root, "status").c_str(), "ok");
    EXPECT_STREQ(field_str(start_doc.root, "action").c_str(), "screen.dump");
    EXPECT_STREQ(field_str(start_doc.root, "event").c_str(), "start");
    const cJSON *size = field(start_doc.root, "size");
    const cJSON *chunks = field(start_doc.root, "chunks");
    ASSERT_TRUE(cJSON_IsNumber(size)) << "start precisa reportar size numérico";
    ASSERT_TRUE(cJSON_IsNumber(chunks)) << "start precisa reportar chunks numérico";
    EXPECT_EQ(static_cast<size_t>(size->valuedouble), payload.size());
    EXPECT_GT(chunks->valueint, 0);

    /* 4) Se o dispatch devolveu o stream completo (mais do que o start),
       valida a sequência e reconstrói bytes idênticos. */
    if (frames.size() < 2u) {
        GTEST_SKIP() << "dispatch devolveu apenas o frame start; o stream "
                        "completo (chunks) é exercitado em "
                        "tests/test_tab5_cli_contract.py";
    }

    std::string reassembled;
    int expected_index = 0;
    bool saw_end = false;
    for (size_t i = 1; i < frames.size(); ++i) {
        JsonDoc doc(frames[i]);
        ASSERT_NE(doc.root, nullptr) << "frame inválido de screen.dump: " << frames[i];
        const std::string event = field_str(doc.root, "event");
        if (event == "end") {
            saw_end = true;
            EXPECT_EQ(i, frames.size() - 1) << "end precisa ser o último frame";
            break;
        }
        ASSERT_STREQ(field_str(doc.root, "action").c_str(), "screen.dump");
        const cJSON *chunk = field(doc.root, "chunk");
        ASSERT_TRUE(cJSON_IsNumber(chunk)) << "frame chunk precisa ter índice numérico";
        EXPECT_EQ(chunk->valueint, expected_index) << "chunks precisam vir em ordem 0..N-1";
        ++expected_index;
        const cJSON *b64 = field(doc.root, "b64");
        ASSERT_TRUE(cJSON_IsString(b64) && b64->valuestring != nullptr) << "frame chunk precisa ter bloco base64";
        reassembled += base64_decode(b64->valuestring);
    }

    EXPECT_TRUE(saw_end) << "stream de screen.dump precisa terminar com event=end";
    EXPECT_EQ(expected_index, chunks->valueint) << "quantidade de chunks deve bater com o start";
    EXPECT_EQ(reassembled.size(), payload.size()) << "tamanho reconstruído != original";
    EXPECT_EQ(reassembled, payload) << "RECONSTRUÇÃO NÃO é byte-idêntica ao arquivo original";
}

// ---------------------------------------------------------------------------
// Diagnóstico e sistema (PLANO §2.2 item 5)
// ---------------------------------------------------------------------------

TEST(SerialBridgeDispatchContract, SysInfoRespondsOkWithData)
{
    const auto r = run_dispatch("{\"cmd\":\"sys.info\"}");
    ASSERT_GT(r.rc, 0);
    const auto frames = split_frames(r.text);
    ASSERT_EQ(frames.size(), 1u);
    expect_ok_frame(frames[0], "");
    JsonDoc doc(frames[0]);
    ASSERT_NE(doc.root, nullptr);
    const cJSON *data = field(doc.root, "data");
    ASSERT_NE(data, nullptr) << "sys.info precisa carregar data";
    /* Pelos critérios do plano §4.2: heap, bateria e Wi-Fi presentes. */
    EXPECT_TRUE(field(data, "heap_free_internal") != nullptr || field(data, "heap_free_psram") != nullptr)
        << "sys.info precisa incluir heap";
    EXPECT_TRUE(field(data, "battery_mv") != nullptr || field(data, "battery_pct") != nullptr)
        << "sys.info precisa incluir bateria";
    EXPECT_TRUE(field(data, "wifi_connected") != nullptr || field(data, "wifi_ssid") != nullptr ||
                field(data, "wifi_ip") != nullptr)
        << "sys.info precisa incluir estado do Wi-Fi";
}

TEST(SerialBridgeDispatchContract, SysIdleTogglesAndReportsState)
{
    const auto enabled = run_dispatch("{\"cmd\":\"sys.idle\",\"enable\":true}");
    ASSERT_GT(enabled.rc, 0);
    const auto enabled_frames = split_frames(enabled.text);
    ASSERT_EQ(enabled_frames.size(), 1u);
    expect_ok_frame(enabled_frames[0], "sys.idle");
    JsonDoc enabled_doc(enabled_frames[0]);
    ASSERT_TRUE(cJSON_IsTrue(field(field(enabled_doc.root, "data"), "enabled")));

    const auto disabled = run_dispatch("{\"cmd\":\"sys.idle\",\"enable\":false}");
    ASSERT_GT(disabled.rc, 0);
    const auto disabled_frames = split_frames(disabled.text);
    ASSERT_EQ(disabled_frames.size(), 1u);
    expect_ok_frame(disabled_frames[0], "sys.idle");
    JsonDoc disabled_doc(disabled_frames[0]);
    ASSERT_TRUE(cJSON_IsFalse(field(field(disabled_doc.root, "data"), "enabled")));
}

TEST(SerialBridgeDispatchContract, ScreenDumpRetryStartsAtRequestedChunkAndEnds)
{
    const int mk = ::mkdir("/sdcard/screenshots", 0755);
    ASSERT_TRUE(mk == 0 || errno == EEXIST) << "fixture directory setup failed";
    const char *virt = "/sdcard/screenshots/retry_contract.bmp";
    const std::string payload = create_screenshot_fixture();
    FILE *f = fopen(virt, "wb");
    ASSERT_NE(f, nullptr);
    ASSERT_EQ(fwrite(payload.data(), 1, payload.size(), f), payload.size());
    fclose(f);

    const auto result =
        run_dispatch(std::string("{\"cmd\":\"screen.dump.retry\",\"path\":\"") + virt + "\",\"from\":1}");
    ASSERT_GT(result.rc, 0);
    const auto frames = split_frames(result.text);
    ASSERT_GE(frames.size(), 2u);
    JsonDoc start(frames.front());
    ASSERT_EQ(field_str(start.root, "event"), "start");
    const int total = field(start.root, "chunks")->valueint;
    ASSERT_GT(total, 1);
    int expected = 1;
    bool saw_end = false;
    for (size_t i = 1; i < frames.size(); ++i) {
        JsonDoc doc(frames[i]);
        if (field_str(doc.root, "event") == "end") {
            saw_end = true;
            ASSERT_EQ(i + 1, frames.size());
            continue;
        }
        ASSERT_EQ(field(doc.root, "chunk")->valueint, expected++);
    }
    EXPECT_EQ(expected, total);
    EXPECT_TRUE(saw_end);
}

TEST(SerialBridgeDispatchContract, ScreenDumpRejectsMissingAndEscapingPaths)
{
    for (const std::string &path : {"/sdcard/screenshots/no-such-file.bmp", "/sdcard/screenshots/../private.bmp"}) {
        const auto result = run_dispatch(std::string("{\"cmd\":\"screen.dump\",\"path\":\"") + path + "\"}");
        ASSERT_GT(result.rc, 0);
        const auto frames = split_frames(result.text);
        ASSERT_GE(frames.size(), 1u);
        expect_error_frame(frames.front());
    }
}

TEST(SerialBridgeDispatchContract, SysIdleMalformedEnableKeepsProtocolResponse)
{
    const auto result = run_dispatch("{\"cmd\":\"sys.idle\",\"enable\":\"yes\"}");
    ASSERT_GT(result.rc, 0);
    const auto frames = split_frames(result.text);
    ASSERT_EQ(frames.size(), 1u);
    expect_ok_frame(frames.front(), "sys.idle");
}

#endif /* SERIAL_BRIDGE_HEADER_AVAILABLE */
