#include "tab5_sdk.h"
#include "terminal_cmd.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <dirent.h>
#include <regex>
#include <string>

#include <gtest/gtest.h>

namespace {

class TerminalMicroShellTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        std::string tmpl = "/tmp/tab5_shell_XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        const char *dir = mkdtemp(buf.data());
        ASSERT_NE(dir, nullptr);
        cwd = dir;
    }

    void TearDown() override
    {
        DIR *d = opendir(cwd.c_str());
        if (d == nullptr) {
            return;
        }
        struct dirent *ent;
        while ((ent = readdir(d)) != nullptr) {
            if (std::strcmp(ent->d_name, ".") == 0 || std::strcmp(ent->d_name, "..") == 0) {
                continue;
            }
            const std::string full = cwd + "/" + ent->d_name;
            struct stat st;
            if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                rmdir(full.c_str());
            } else {
                unlink(full.c_str());
            }
        }
        rmdir(cwd.c_str());
    }

    std::string cwd;
};

// ---------------------------------------------------------------------------
// free/df/date: comandos novos do Micro-Shell (plano de features)
// ---------------------------------------------------------------------------

TEST_F(TerminalMicroShellTest, FreeRetornaValoresDeMemoria)
{
    const std::string saida = terminal_exec("free", cwd);
    ASSERT_FALSE(saida.empty()) << "free sem saída";
    // Contrato (test_terminal_micro_shell_contract.py): valores numéricos
    // (memória alocada/livre) em seção nomeada para o usuário.
    EXPECT_TRUE(std::regex_search(saida, std::regex(R"(\d)"))) << "free sem nenhum valor numérico: " << saida;
    // std::regex ECMAScript (libstdc++) não aceita o modificador inline
    // "(?i)"; usa a flag icase, mantendo o mesmo critério.
    EXPECT_TRUE(
        std::regex_search(saida, std::regex(R"(mem|heap|us[ae]d|livre|free|total)", std::regex_constants::icase)))
        << "free sem seção nomeada: " << saida;
    // Toda saída do Micro-Shell termina em newline.
    EXPECT_EQ(saida.back(), '\n') << "free deve terminar em newline";
}

TEST_F(TerminalMicroShellTest, DfRetornaFilesystemComInformacaoDeEspaco)
{
    const std::string saida = terminal_exec("df", cwd);
    ASSERT_FALSE(saida.empty()) << "df sem saída";
    // Contrato: menciona o caminho alvo (default = diretório corrente); o
    // comando exibe o alvo tanto no sucesso quanto na mensagem de erro.
    EXPECT_NE(saida.find(cwd), std::string::npos) << "df não menciona o alvo: " << saida;
    EXPECT_TRUE(std::regex_search(saida, std::regex(R"(\d)"))) << "df sem valores de espaço: " << saida;
}

TEST_F(TerminalMicroShellTest, DateRetornaDatareHoraValidas)
{
    const std::string saida = terminal_exec("date", cwd);
    EXPECT_FALSE(saida.empty());
    // Formatos aceitos: ISO (2026-09-14) ou ctime-style (Mon Sep 14 22:10:00 2026).
    const bool iso = std::regex_search(saida, std::regex(R"(\d{4}-\d{2}-\d{2})"));
    const bool ctime =
        std::regex_search(saida, std::regex(R"([A-Z][a-z]{2} [A-Z][a-z]{2} \d{1,2} \d{2}:\d{2}:\d{2} \d{4})"));
    EXPECT_TRUE(iso || ctime) << "date com formato inválido: " << saida;
}

// ---------------------------------------------------------------------------
// help: núcleo do Micro-Shell incluindo os comandos novos
// ---------------------------------------------------------------------------

TEST_F(TerminalMicroShellTest, HelpListaNucleoDoMicroShell)
{
    const std::string saida = terminal_exec("help", cwd);
    for (const char *cmd : {"help", "ls", "cd", "pwd", "free", "df", "date", "clear"}) {
        SCOPED_TRACE(cmd);
        EXPECT_NE(saida.find(cmd), std::string::npos) << "help não lista '" << cmd << "': " << saida;
    }
}

TEST_F(TerminalMicroShellTest, HelpVezes30EstavelELimitado)
{
    const std::string primeira = terminal_exec("help", cwd);
    EXPECT_FALSE(primeira.empty());
    EXPECT_LE(primeira.size(), 2048u) << "help acima de 2048 bytes";
    EXPECT_EQ(primeira.back(), '\n') << "help deve terminar em newline";
    for (int i = 0; i < 30; ++i) {
        const std::string atual = terminal_exec("help", cwd);
        EXPECT_EQ(atual, primeira) << "help não-determinístico na iteração " << i;
    }
}

TEST_F(TerminalMicroShellTest, ComandosNovosSemArgumentoNaoCrasham)
{
    for (const char *cmd : {"free", "df", "date"}) {
        SCOPED_TRACE(cmd);
        const std::string saida = terminal_exec(cmd, cwd);
        EXPECT_FALSE(saida.empty());
    }
    // clear mantém o comportamento histórico de limpeza (\x0C).
    EXPECT_EQ(terminal_exec("clear", cwd), "\x0C");
}

// ---------------------------------------------------------------------------
// tab5_terminal_exec (ABI): fallback host roteia para o Micro-Shell real
// ---------------------------------------------------------------------------

TEST_F(TerminalMicroShellTest, Tab5TerminalExecRoteiaHelpParaMicroShell)
{
    char out[2048] = {0};
    EXPECT_EQ(tab5_terminal_exec("help", out, sizeof(out)), TAB5_OK);
    const std::string saida(out);
    EXPECT_NE(saida.find("Tab5-OS Shell"), std::string::npos)
        << "fallback host NÃO roteia para terminal_exec (stub 'Output of...'?): " << saida;
    for (const char *cmd : {"free", "df", "date", "ls", "clear", "help"}) {
        SCOPED_TRACE(cmd);
        EXPECT_NE(saida.find(cmd), std::string::npos) << "help via ABI sem '" << cmd << "'";
    }
}

TEST_F(TerminalMicroShellTest, Tab5TerminalExecTruncaComNul)
{
    char out[16] = {0};
    EXPECT_EQ(tab5_terminal_exec("help", out, sizeof(out)), TAB5_OK);
    EXPECT_LT(std::strlen(out), sizeof(out)) << "saída não truncada no buffer";
    EXPECT_EQ(out[15], '\0') << "falta NUL no fim do buffer (read overflow)";
}

TEST_F(TerminalMicroShellTest, Tab5TerminalExecRejeitaArgumentosInvalidos)
{
    char out[64] = {0};
    EXPECT_EQ(tab5_terminal_exec(nullptr, out, sizeof(out)), TAB5_ERR_INVALID_ARG);
    EXPECT_EQ(tab5_terminal_exec("help", nullptr, sizeof(out)), TAB5_ERR_INVALID_ARG);
    EXPECT_EQ(tab5_terminal_exec("help", out, 0), TAB5_ERR_INVALID_ARG);
}

} // namespace
