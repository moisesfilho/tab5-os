#include "ai_storage.h"
#include "path_redirect.hpp"

#include <fstream>
#include <iterator>
#include <string>

#include <gtest/gtest.h>

namespace {

class AiStorageTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        hostmock::unlink_files({AI_CFG_PATH});
    }

    static void escrever_cfg(const std::string &conteudo)
    {
        std::ofstream out(hostmock::host_of(AI_CFG_PATH), std::ios::trunc);
        out << conteudo;
    }
};

TEST_F(AiStorageTest, DefaultsPopulamTodosOsCampos)
{
    ai_cfg_t cfg = {};
    ai_storage_get_default(&cfg);
    EXPECT_STREQ(cfg.base_url, AI_DEFAULT_BASE_URL);
    EXPECT_STREQ(cfg.token, "");
    EXPECT_STREQ(cfg.model, AI_DEFAULT_MODEL);
    EXPECT_EQ(cfg.max_tokens, AI_DEFAULT_MAX_TOKENS);
    EXPECT_EQ(cfg.timeout_sec, AI_DEFAULT_TIMEOUT_SEC);

    /* Null seguro */
    ai_storage_get_default(nullptr);
}

TEST_F(AiStorageTest, LoadNullRetornaInvalidArg)
{
    EXPECT_EQ(ai_storage_load(nullptr), ESP_ERR_INVALID_ARG);
}

TEST_F(AiStorageTest, LoadSemArquivoRetornaNotFoundComDefaults)
{
    ai_cfg_t cfg = {};
    EXPECT_EQ(ai_storage_load(&cfg), ESP_ERR_NOT_FOUND);
    EXPECT_STREQ(cfg.base_url, AI_DEFAULT_BASE_URL);
    EXPECT_EQ(cfg.max_tokens, AI_DEFAULT_MAX_TOKENS);
}

TEST_F(AiStorageTest, SaveLoadRoundTripValoresCustomizados)
{
    ai_cfg_t cfg = {};
    snprintf(cfg.base_url, sizeof(cfg.base_url), "https://api.exemplo.dev/v1");
    snprintf(cfg.token, sizeof(cfg.token), "tok-abc123");
    snprintf(cfg.model, sizeof(cfg.model), "modelo-x");
    cfg.max_tokens = 4096;
    cfg.timeout_sec = 60;

    ASSERT_EQ(ai_storage_save(&cfg), ESP_OK);

    ai_cfg_t lida = {};
    ASSERT_EQ(ai_storage_load(&lida), ESP_OK);
    EXPECT_STREQ(lida.base_url, "https://api.exemplo.dev/v1");
    EXPECT_STREQ(lida.token, "tok-abc123");
    EXPECT_STREQ(lida.model, "modelo-x");
    EXPECT_EQ(lida.max_tokens, 4096);
    EXPECT_EQ(lida.timeout_sec, 60);
}

TEST_F(AiStorageTest, RangeDeMaxTokensETimeoutValidado)
{
    /* Valores fora da faixa sao descartados e mantem os defaults */
    escrever_cfg("max_tokens=99999\ntimeout_sec=1\n");
    ai_cfg_t cfg = {};
    ASSERT_EQ(ai_storage_load(&cfg), ESP_OK);
    EXPECT_EQ(cfg.max_tokens, AI_DEFAULT_MAX_TOKENS);
    EXPECT_EQ(cfg.timeout_sec, AI_DEFAULT_TIMEOUT_SEC);

    /* Limites superiores aceitos */
    escrever_cfg("max_tokens=8192\ntimeout_sec=300\n");
    ASSERT_EQ(ai_storage_load(&cfg), ESP_OK);
    EXPECT_EQ(cfg.max_tokens, 8192);
    EXPECT_EQ(cfg.timeout_sec, 300);
}

TEST_F(AiStorageTest, ComentariosTrimEVariacoesDeEspaco)
{
    escrever_cfg("# comentario\n"
                 "; outro comentario\n"
                 " base_url = https://x.y/v1 \n"
                 "\ttoken\t=\t tkn123 \n"
                 "model=gpt\n");

    ai_cfg_t cfg = {};
    ASSERT_EQ(ai_storage_load(&cfg), ESP_OK);
    EXPECT_STREQ(cfg.base_url, "https://x.y/v1");
    EXPECT_STREQ(cfg.token, "tkn123");
    EXPECT_STREQ(cfg.model, "gpt");
}

TEST_F(AiStorageTest, SaveNullRetornaInvalidArg)
{
    EXPECT_EQ(ai_storage_save(nullptr), ESP_ERR_INVALID_ARG);
}

} // namespace
