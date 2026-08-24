#include "terminal_cmd.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <dirent.h>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

class TerminalCmdTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        std::string tmpl = "/tmp/tab5_terminal_XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        const char *dir = mkdtemp(buf.data());
        ASSERT_NE(dir, nullptr);
        cwd = dir;
    }

    void TearDown() override
    {
        remover_arvore(cwd);
    }

    std::string caminho(const std::string &nome) const
    {
        return cwd + "/" + nome;
    }

    void criar_arquivo(const std::string &nome, const std::string &conteudo)
    {
        std::ofstream out(caminho(nome), std::ios::binary | std::ios::trunc);
        out << conteudo;
    }

    static void remover_arvore(const std::string &raiz)
    {
        DIR *d = opendir(raiz.c_str());
        if (d == nullptr) {
            return;
        }
        struct dirent *ent;
        while ((ent = readdir(d)) != nullptr) {
            if (std::strcmp(ent->d_name, ".") == 0 || std::strcmp(ent->d_name, "..") == 0) {
                continue;
            }
            const std::string full = raiz + "/" + ent->d_name;
            struct stat st;
            if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                remover_arvore(full);
            } else {
                unlink(full.c_str());
            }
        }
        closedir(d);
        rmdir(raiz.c_str());
    }

    std::string cwd;
};

TEST_F(TerminalCmdTest, LinhaVaziaNaoProduzSaida)
{
    EXPECT_EQ(terminal_exec("", cwd), "");
    EXPECT_EQ(terminal_exec("   \t ", cwd), "");
}

TEST_F(TerminalCmdTest, EchoJuntaTokensRespeitandoAspas)
{
    EXPECT_EQ(terminal_exec("echo a \"b c\" 'd e'", cwd), "a b c d e\n");
    EXPECT_EQ(terminal_exec("echo", cwd), "\n");
}

TEST_F(TerminalCmdTest, MkdirMultiOperandoECriaPastas)
{
    EXPECT_EQ(terminal_exec("mkdir d1 d2", cwd), "");
    struct stat st;
    ASSERT_EQ(stat(caminho("d1").c_str(), &st), 0);
    EXPECT_TRUE(S_ISDIR(st.st_mode));
    ASSERT_EQ(stat(caminho("d2").c_str(), &st), 0);
    EXPECT_TRUE(S_ISDIR(st.st_mode));
    EXPECT_NE(terminal_exec("mkdir", cwd).find("missing operand"), std::string::npos);
}

TEST_F(TerminalCmdTest, TouchCatFluxoCompleto)
{
    criar_arquivo("nota.txt", "hello");
    EXPECT_EQ(terminal_exec("cat nota.txt", cwd), "hello\n");
    /* touch em arquivo existente apenas reabre (nao apaga o conteudo) */
    EXPECT_EQ(terminal_exec("touch nota.txt", cwd), "");
    EXPECT_EQ(terminal_exec("cat nota.txt", cwd), "hello\n");
    EXPECT_EQ(terminal_exec("touch novo.txt", cwd), "");
    EXPECT_NE(terminal_exec("cat nada.txt", cwd).find("No such file"), std::string::npos);
    EXPECT_NE(terminal_exec("cat", cwd).find("missing operand"), std::string::npos);
}

TEST_F(TerminalCmdTest, CatDiretorioReportaErro)
{
    EXPECT_EQ(terminal_exec("mkdir docs", cwd), "");
    EXPECT_NE(terminal_exec("cat docs", cwd).find("Is a directory"), std::string::npos);
}

TEST_F(TerminalCmdTest, CatTruncaArquivoGrandeEm4KB)
{
    criar_arquivo("grande.log", std::string(5000, 'x'));
    const std::string saida = terminal_exec("cat grande.log", cwd);
    EXPECT_NE(saida.find("[... truncated at 4KB ...]"), std::string::npos);
}

TEST_F(TerminalCmdTest, LsOrdenaPastasPrimeiroEFormataTamanhos)
{
    EXPECT_EQ(mkdir(caminho("zdir").c_str(), 0755), 0);
    criar_arquivo("alfa.txt", std::string(10, 'a'));
    criar_arquivo("beta.txt", std::string(2048, 'b'));

    const std::string saida = terminal_exec("ls", cwd);
    const size_t pos_dir = saida.find("<DIR>");
    const size_t pos_a = saida.find("alfa.txt");
    const size_t pos_b = saida.find("beta.txt");
    ASSERT_NE(pos_dir, std::string::npos);
    ASSERT_NE(pos_a, std::string::npos);
    ASSERT_NE(pos_b, std::string::npos);
    EXPECT_LT(pos_dir, pos_a);
    EXPECT_LT(pos_a, pos_b);
    EXPECT_NE(saida.find("   10 B"), std::string::npos);
    EXPECT_NE(saida.find("  2.0 KB"), std::string::npos);
    EXPECT_NE(saida.find("zdir/"), std::string::npos);
}

TEST_F(TerminalCmdTest, LsDiretorioVazioMostraPlaceholder)
{
    EXPECT_EQ(terminal_exec("ls", cwd), "(empty directory)\n");
    EXPECT_NE(terminal_exec("ls pasta_fantasma", cwd).find("cannot access"), std::string::npos);
}

TEST_F(TerminalCmdTest, CdNavegaRelativoAbsolutoEPai)
{
    const std::string base = cwd;
    EXPECT_EQ(terminal_exec("mkdir sub aninhado_base", cwd), "");

    EXPECT_EQ(terminal_exec("cd sub", cwd), "");
    EXPECT_EQ(cwd, base + "/sub");

    EXPECT_EQ(terminal_exec("cd ..", cwd), "");
    EXPECT_EQ(cwd, base);

    EXPECT_EQ(terminal_exec("cd ./aninhado_base/../sub", cwd), "");
    EXPECT_EQ(cwd, base + "/sub");

    EXPECT_EQ(terminal_exec("cd ..", cwd), "");
    EXPECT_EQ(cwd, base);
    EXPECT_EQ(terminal_exec("pwd", cwd), base + "\n");
    EXPECT_NE(terminal_exec("cd /definitivamente/inexistente", cwd).find("No such directory"), std::string::npos);
    /* "~" resolve para /sdcard, que nao existe no host */
    EXPECT_NE(terminal_exec("cd ~", cwd).find("cd: /sdcard"), std::string::npos);
}

TEST_F(TerminalCmdTest, CdRecusaArquivoComoDestino)
{
    criar_arquivo("plano.txt", "x");
    EXPECT_NE(terminal_exec("cd plano.txt", cwd).find("Not a directory"), std::string::npos);
}

TEST_F(TerminalCmdTest, RmRemoveArquivoEDiretorioVazioEReportaErros)
{
    criar_arquivo("temp.txt", "x");
    EXPECT_EQ(terminal_exec("mkdir vazio", cwd), "");

    EXPECT_EQ(terminal_exec("rm temp.txt", cwd), "");
    struct stat st;
    EXPECT_NE(stat(caminho("temp.txt").c_str(), &st), 0);

    EXPECT_NE(terminal_exec("rm temp.txt", cwd).find("No such file or directory"), std::string::npos);
    EXPECT_NE(terminal_exec("rm", cwd).find("missing operand"), std::string::npos);

    /* rm em pasta cheia falha com mensagem de diretorio */
    EXPECT_EQ(terminal_exec("mkdir cheia", cwd), "");
    criar_arquivo("cheia/dentro.txt", "x");
    EXPECT_NE(terminal_exec("rm cheia", cwd).find("cannot remove directory"), std::string::npos);

    /* rmdir so aceita pastas vazias */
    EXPECT_EQ(terminal_exec("rmdir vazio", cwd), "");
    EXPECT_NE(terminal_exec("rmdir cheia", cwd).find("failed to remove"), std::string::npos);
    EXPECT_NE(terminal_exec("rmdir", cwd).find("missing operand"), std::string::npos);
}

TEST_F(TerminalCmdTest, ComandoDesconhecidoReportaErro)
{
    const std::string saida = terminal_exec("sl", cwd);
    EXPECT_NE(saida.find("command not found"), std::string::npos);
    EXPECT_NE(saida.find("help"), std::string::npos);
}

TEST_F(TerminalCmdTest, ComandosInternosProduzemSaidaEsperada)
{
    EXPECT_EQ(terminal_exec("clear", cwd), "\x0C");
    EXPECT_EQ(terminal_exec("whoami", cwd), "root@tab5\n");
    EXPECT_EQ(terminal_exec("uname", cwd), "Tab5-OS ESP32-P4 FreeRTOS/LVGL9 (RISC-V)\n");
    EXPECT_NE(terminal_exec("help", cwd).find("ssh [user@]host"), std::string::npos);
}

TEST_F(TerminalCmdTest, ParseSshPadraoRootPorta22)
{
    std::string user, host, err;
    int port = 0;
    ASSERT_TRUE(terminal_parse_ssh_cmd("ssh 192.168.1.50", user, host, port, err));
    EXPECT_EQ(user, "root");
    EXPECT_EQ(host, "192.168.1.50");
    EXPECT_EQ(port, 22);
    EXPECT_TRUE(err.empty());
}

TEST_F(TerminalCmdTest, ParseSshUsuarioHostPortaCustomizada)
{
    std::string user, host, err;
    int port = 0;
    ASSERT_TRUE(terminal_parse_ssh_cmd("ssh admin@10.0.0.1 -p 2222", user, host, port, err));
    EXPECT_EQ(user, "admin");
    EXPECT_EQ(host, "10.0.0.1");
    EXPECT_EQ(port, 2222);
    EXPECT_TRUE(err.empty());
}

TEST_F(TerminalCmdTest, ParseSshRejeitaPortasInvalidas)
{
    const char *linhas[] = {"ssh host -p abc", "ssh host -p 0", "ssh host -p 70000", "ssh host -p 22x"};
    for (const char *linha : linhas) {
        SCOPED_TRACE(linha);
        std::string user, host, err;
        int port = 0;
        ASSERT_TRUE(terminal_parse_ssh_cmd(linha, user, host, port, err));
        EXPECT_EQ(port, 22);
        EXPECT_FALSE(err.empty());
    }
}

TEST_F(TerminalCmdTest, ParseSshSemHostGeraMensagemDeUso)
{
    std::string user, host, err;
    int port = 0;
    ASSERT_TRUE(terminal_parse_ssh_cmd("ssh", user, host, port, err));
    EXPECT_NE(err.find("Uso:"), std::string::npos);
}

TEST_F(TerminalCmdTest, ParseLinhaQueNaoESshRetornaFalso)
{
    std::string user, host, err;
    int port = 0;
    EXPECT_FALSE(terminal_parse_ssh_cmd("ls -la", user, host, port, err));
    EXPECT_FALSE(terminal_parse_ssh_cmd("", user, host, port, err));
}

} // namespace
