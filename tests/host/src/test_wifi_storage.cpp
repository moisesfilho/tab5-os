#include "wifi_storage.h"
#include "path_redirect.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include <gtest/gtest.h>

namespace {

class WifiStorageTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        /* Espelha o ciclo real: o SD ja esta montado antes do uso das APIs */
        ASSERT_EQ(wifi_storage_mount(), ESP_OK);
        hostmock::unlink_files({WIFI_CFG_PATH});
    }

    static void escrever_cfg(const std::string &conteudo)
    {
        std::ofstream out(hostmock::host_of(WIFI_CFG_PATH), std::ios::trunc);
        out << conteudo;
    }
};

TEST_F(WifiStorageTest, MountEIdempotente)
{
    EXPECT_EQ(wifi_storage_mount(), ESP_OK);
    EXPECT_EQ(wifi_storage_mount(), ESP_OK);
}

TEST_F(WifiStorageTest, LoadAllSemArquivoRetornaNotFound)
{
    wifi_saved_list_t lista = {};
    EXPECT_EQ(wifi_storage_load_all(&lista), ESP_ERR_NOT_FOUND);
    EXPECT_EQ(lista.count, 0);
}

TEST_F(WifiStorageTest, SaveLoadAllRoundTrip)
{
    wifi_saved_list_t original = {};
    snprintf(original.items[0].ssid, sizeof(original.items[0].ssid), "Casa");
    snprintf(original.items[0].password, sizeof(original.items[0].password), "senha-casa");
    snprintf(original.items[1].ssid, sizeof(original.items[1].ssid), "Trabalho");
    snprintf(original.items[1].password, sizeof(original.items[1].password), "pwd-trabalho");
    original.count = 2;

    ASSERT_EQ(wifi_storage_save_all(&original), ESP_OK);

    wifi_saved_list_t lida = {};
    ASSERT_EQ(wifi_storage_load_all(&lida), ESP_OK);
    ASSERT_EQ(lida.count, 2);
    EXPECT_STREQ(lida.items[0].ssid, "Casa");
    EXPECT_STREQ(lida.items[0].password, "senha-casa");
    EXPECT_STREQ(lida.items[1].ssid, "Trabalho");
    EXPECT_STREQ(lida.items[1].password, "pwd-trabalho");
}

TEST_F(WifiStorageTest, AddUpdateRemoveFindFluxoCompleto)
{
    EXPECT_EQ(wifi_storage_add_or_update("NetA", "senha-a"), ESP_OK);
    EXPECT_EQ(wifi_storage_add_or_update("NetB", "senha-b"), ESP_OK);

    /* Atualizacao de rede existente nao duplica entrada */
    EXPECT_EQ(wifi_storage_add_or_update("NetA", "senha-nova"), ESP_OK);

    char senha[65] = {};
    EXPECT_TRUE(wifi_storage_find("NetA", senha, sizeof(senha)));
    EXPECT_STREQ(senha, "senha-nova");

    EXPECT_EQ(wifi_storage_remove("NetB"), ESP_OK);
    EXPECT_FALSE(wifi_storage_find("NetB", nullptr, 0));

    wifi_saved_list_t lista = {};
    ASSERT_EQ(wifi_storage_load_all(&lista), ESP_OK);
    ASSERT_EQ(lista.count, 1);
    EXPECT_STREQ(lista.items[0].ssid, "NetA");
}

TEST_F(WifiStorageTest, ParsingSecoesComentariosETrimDeChave)
{
    escrever_cfg("# comentario inicial\n"
                 "; estilo ini\n"
                 "\n"
                 "[Rede Casa]\n"
                 "password=segredo123\n"
                 "\n"
                 " ssid =Segunda\n"
                 "pwd=pwd2\n");

    wifi_saved_list_t lista = {};
    ASSERT_EQ(wifi_storage_load_all(&lista), ESP_OK);
    ASSERT_EQ(lista.count, 2);
    EXPECT_STREQ(lista.items[0].ssid, "Rede Casa");
    EXPECT_STREQ(lista.items[0].password, "segredo123");
    EXPECT_STREQ(lista.items[1].ssid, "Segunda");
    EXPECT_STREQ(lista.items[1].password, "pwd2");
}

TEST_F(WifiStorageTest, LimiteDezesseisRedesEvictaMaisAntiga)
{
    char ssid[33];
    for (int i = 0; i < WIFI_MAX_SAVED_NETWORKS; ++i) {
        snprintf(ssid, sizeof(ssid), "net%d", i);
        ASSERT_EQ(wifi_storage_add_or_update(ssid, "x"), ESP_OK);
    }
    EXPECT_EQ(wifi_storage_add_or_update("net16", "x"), ESP_OK);

    wifi_saved_list_t lista = {};
    ASSERT_EQ(wifi_storage_load_all(&lista), ESP_OK);
    EXPECT_EQ(lista.count, WIFI_MAX_SAVED_NETWORKS);
    EXPECT_FALSE(wifi_storage_find("net0", nullptr, 0));
    EXPECT_TRUE(wifi_storage_find("net1", nullptr, 0));
    EXPECT_TRUE(wifi_storage_find("net16", nullptr, 0));
}

TEST_F(WifiStorageTest, RetrocompatibilidadeCarregaPrimeiraRede)
{
    wifi_saved_list_t lista = {};
    snprintf(lista.items[0].ssid, sizeof(lista.items[0].ssid), "Casa");
    snprintf(lista.items[0].password, sizeof(lista.items[0].password), "senha-casa");
    snprintf(lista.items[1].ssid, sizeof(lista.items[1].ssid), "Trabalho");
    lista.count = 2;
    ASSERT_EQ(wifi_storage_save_all(&lista), ESP_OK);

    wifi_cfg_t cfg = {};
    ASSERT_EQ(wifi_storage_load(&cfg), ESP_OK);
    EXPECT_STREQ(cfg.ssid, "Casa");
    EXPECT_STREQ(cfg.password, "senha-casa");

    hostmock::unlink_files({WIFI_CFG_PATH});
    EXPECT_EQ(wifi_storage_load(&cfg), ESP_ERR_NOT_FOUND);
}

TEST_F(WifiStorageTest, SaveCfgCompatGravaComoUnicaRede)
{
    wifi_cfg_t cfg = {};
    snprintf(cfg.ssid, sizeof(cfg.ssid), "Solo");
    snprintf(cfg.password, sizeof(cfg.password), "p1");
    EXPECT_EQ(wifi_storage_save(&cfg), ESP_OK);

    wifi_saved_list_t lista = {};
    ASSERT_EQ(wifi_storage_load_all(&lista), ESP_OK);
    ASSERT_EQ(lista.count, 1);
    EXPECT_STREQ(lista.items[0].ssid, "Solo");

    EXPECT_EQ(wifi_storage_save(nullptr), ESP_ERR_INVALID_ARG);
}

TEST_F(WifiStorageTest, ArgumentosInvalidosERedesInexistentes)
{
    EXPECT_EQ(wifi_storage_load_all(nullptr), ESP_ERR_INVALID_ARG);
    EXPECT_EQ(wifi_storage_save_all(nullptr), ESP_ERR_INVALID_ARG);
    EXPECT_EQ(wifi_storage_load(nullptr), ESP_ERR_INVALID_ARG);

    EXPECT_EQ(wifi_storage_add_or_update(nullptr, "x"), ESP_ERR_INVALID_ARG);
    EXPECT_EQ(wifi_storage_add_or_update("", "x"), ESP_ERR_INVALID_ARG);

    EXPECT_EQ(wifi_storage_remove(nullptr), ESP_ERR_INVALID_ARG);
    EXPECT_EQ(wifi_storage_remove(""), ESP_ERR_INVALID_ARG);
    EXPECT_EQ(wifi_storage_remove("fantasma"), ESP_ERR_NOT_FOUND);

    char senha[8];
    EXPECT_FALSE(wifi_storage_find(nullptr, senha, sizeof(senha)));
    EXPECT_FALSE(wifi_storage_find("", senha, sizeof(senha)));
}

} // namespace
