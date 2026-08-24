#include "file_assoc.h"

#include <gtest/gtest.h>
#include <string>

namespace {

std::string g_last_path;
int g_handler_calls = 0;

void handler_principal(const char *path)
{
    g_last_path = path != nullptr ? path : "";
    ++g_handler_calls;
}

void handler_alternativo(const char *) {}

class FileAssocTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        file_assoc_init();
        g_last_path.clear();
        g_handler_calls = 0;
    }
};

TEST_F(FileAssocTest, RegisterValidaArgumentos)
{
    EXPECT_EQ(file_assoc_register(nullptr, handler_principal), ESP_ERR_INVALID_ARG);
    EXPECT_EQ(file_assoc_register(".txt", nullptr), ESP_ERR_INVALID_ARG);
    EXPECT_EQ(file_assoc_register("", handler_principal), ESP_ERR_INVALID_ARG);
}

TEST_F(FileAssocTest, RegisterNormalizaPontoEMaiusculas)
{
    ASSERT_EQ(file_assoc_register(".TXT", handler_principal), ESP_OK);
    EXPECT_EQ(file_assoc_open("documento.txt"), ESP_OK);
    EXPECT_EQ(g_last_path, "documento.txt");
}

TEST_F(FileAssocTest, RegisterDuplicadoSubstituiHandler)
{
    ASSERT_EQ(file_assoc_register("md", handler_alternativo), ESP_OK);
    ASSERT_EQ(file_assoc_register(".md", handler_principal), ESP_OK);
    EXPECT_EQ(file_assoc_open("leiame.MD"), ESP_OK);
    EXPECT_EQ(g_last_path, "leiame.MD");
}

TEST_F(FileAssocTest, OpenSemExtensaoOuSemRegistroRetornaNotFound)
{
    ASSERT_EQ(file_assoc_register("txt", handler_principal), ESP_OK);
    EXPECT_EQ(file_assoc_open("arquivo_sextensao"), ESP_ERR_NOT_FOUND);
    EXPECT_EQ(file_assoc_open("outro.desconhecido"), ESP_ERR_NOT_FOUND);
    EXPECT_EQ(file_assoc_open(nullptr), ESP_ERR_INVALID_ARG);
    EXPECT_EQ(g_handler_calls, 0);
}

TEST_F(FileAssocTest, OpenDespachaCaminhoExatoUmaVez)
{
    ASSERT_EQ(file_assoc_register("txt", handler_principal), ESP_OK);
    const char *path = "/sdcard/notas/nota_01.txt";
    EXPECT_EQ(file_assoc_open(path), ESP_OK);
    EXPECT_EQ(g_last_path, path);
    EXPECT_EQ(g_handler_calls, 1);
}

TEST_F(FileAssocTest, InitLimpaAssociacoesAnteriores)
{
    ASSERT_EQ(file_assoc_register("log", handler_principal), ESP_OK);
    file_assoc_init();
    EXPECT_EQ(file_assoc_open("sys.log"), ESP_ERR_NOT_FOUND);
    EXPECT_EQ(g_handler_calls, 0);
}

} // namespace
