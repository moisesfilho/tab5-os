#include "bt_storage.h"
#include "path_redirect.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include <gtest/gtest.h>

namespace {

bt_saved_device_t dispositivo(const char *mac, const char *nome, bt_dev_type_t tipo)
{
    bt_saved_device_t dev = {};
    snprintf(dev.mac, sizeof(dev.mac), "%s", mac);
    snprintf(dev.name, sizeof(dev.name), "%s", nome);
    dev.type = tipo;
    dev.addr_type = 0;
    dev.paired = true;
    dev.auto_connect = true;
    return dev;
}

class BtStorageTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        hostmock::unlink_files({BT_CFG_PATH});
    }

    static void escrever_cfg(const std::string &conteudo)
    {
        std::ofstream out(hostmock::host_of(BT_CFG_PATH), std::ios::trunc);
        out << conteudo;
    }
};

/* O modulo mantem cache estatico interno: save_all() o sobrescreve por
 * inteiro, entao cada teste comeca gravando uma base conhecida. */

TEST_F(BtStorageTest, LoadAllSemArquivoESemCacheRetornaNotFound)
{
    /* Deve rodar antes de qualquer save/load que povoe o cache */
    bt_saved_list_t lista = {};
    EXPECT_EQ(bt_storage_load_all(&lista), ESP_ERR_NOT_FOUND);
}

TEST_F(BtStorageTest, SaveLoadRoundTripPreservaCampos)
{
    bt_saved_list_t original = {};
    original.items[0] = dispositivo("AA:BB:CC:DD:EE:01", "Teclado K380", BT_DEV_TYPE_KEYBOARD);
    original.items[0].addr_type = 1;

    original.items[1] = dispositivo("AA:BB:CC:DD:EE:02", "Fone JBL", BT_DEV_TYPE_HEADPHONE);
    original.items[1].paired = false;
    original.items[1].auto_connect = false;
    original.count = 2;

    ASSERT_EQ(bt_storage_save_all(&original), ESP_OK);

    bt_saved_list_t lida = {};
    ASSERT_EQ(bt_storage_load_all(&lida), ESP_OK);
    ASSERT_EQ(lida.count, 2);

    EXPECT_STREQ(lida.items[0].mac, "AA:BB:CC:DD:EE:01");
    EXPECT_STREQ(lida.items[0].name, "Teclado K380");
    EXPECT_EQ(lida.items[0].type, BT_DEV_TYPE_KEYBOARD);
    EXPECT_EQ(lida.items[0].addr_type, 1);
    EXPECT_TRUE(lida.items[0].paired);
    EXPECT_TRUE(lida.items[0].auto_connect);

    EXPECT_STREQ(lida.items[1].mac, "AA:BB:CC:DD:EE:02");
    EXPECT_STREQ(lida.items[1].name, "Fone JBL");
    EXPECT_EQ(lida.items[1].type, BT_DEV_TYPE_HEADPHONE);
    EXPECT_FALSE(lida.items[1].paired);
    EXPECT_FALSE(lida.items[1].auto_connect);
}

TEST_F(BtStorageTest, AddOrUpdateInsereEAtualiza)
{
    bt_saved_list_t base = {};
    ASSERT_EQ(bt_storage_save_all(&base), ESP_OK);

    bt_saved_device_t teclado = dispositivo("AA:00:00:00:00:01", "Teclado", BT_DEV_TYPE_KEYBOARD);
    bt_saved_device_t mouse = dispositivo("AA:00:00:00:00:02", "Mouse", BT_DEV_TYPE_MOUSE);
    ASSERT_EQ(bt_storage_add_or_update(&teclado), ESP_OK);
    ASSERT_EQ(bt_storage_add_or_update(&mouse), ESP_OK);

    snprintf(teclado.name, sizeof(teclado.name), "Teclado Novo");
    ASSERT_EQ(bt_storage_add_or_update(&teclado), ESP_OK);

    bt_saved_device_t achado = {};
    ASSERT_TRUE(bt_storage_find("AA:00:00:00:00:01", &achado));
    EXPECT_STREQ(achado.name, "Teclado Novo");

    bt_saved_list_t do_arquivo = {};
    ASSERT_EQ(bt_storage_load_all(&do_arquivo), ESP_OK);
    ASSERT_EQ(do_arquivo.count, 2);
    EXPECT_STREQ(do_arquivo.items[0].name, "Teclado Novo");
}

TEST_F(BtStorageTest, RemoveDeletaEIgnoraDesconhecidos)
{
    bt_saved_list_t base = {};
    ASSERT_EQ(bt_storage_save_all(&base), ESP_OK);

    bt_saved_device_t dev = dispositivo("AA:00:00:00:00:01", "Qualquer", BT_DEV_TYPE_GENERIC);
    ASSERT_EQ(bt_storage_add_or_update(&dev), ESP_OK);

    EXPECT_EQ(bt_storage_remove("AA:00:00:00:00:01"), ESP_OK);
    EXPECT_FALSE(bt_storage_find("AA:00:00:00:00:01", nullptr));

    /* MAC inexistente responde OK (idempotente) */
    EXPECT_EQ(bt_storage_remove("FF:FF:FF:FF:FF:FF"), ESP_OK);

    bt_saved_list_t do_arquivo = {};
    ASSERT_EQ(bt_storage_load_all(&do_arquivo), ESP_OK);
    EXPECT_EQ(do_arquivo.count, 0);
}

TEST_F(BtStorageTest, FindComparacaoDeMacCaseInsensitive)
{
    bt_saved_list_t base = {};
    ASSERT_EQ(bt_storage_save_all(&base), ESP_OK);

    bt_saved_device_t dev = dispositivo("AA:BB:CC:DD:EE:F0", "K380", BT_DEV_TYPE_KEYBOARD);
    ASSERT_EQ(bt_storage_add_or_update(&dev), ESP_OK);
    EXPECT_TRUE(bt_storage_find("aa:bb:cc:dd:ee:f0", nullptr));
}

TEST_F(BtStorageTest, TypeMappingAliasAudioEFallbackGenerico)
{
    escrever_cfg("[AA:00:00:00:00:0A]\n"
                 "type = audio\n"
                 "[AA:00:00:00:00:0B]\n"
                 "type = mouse\n"
                 "[AA:00:00:00:00:0C]\n"
                 "type = coisa-estranha\n"
                 "[AA:00:00:00:00:0D]\n");

    bt_saved_list_t lista = {};
    ASSERT_EQ(bt_storage_load_all(&lista), ESP_OK);
    ASSERT_EQ(lista.count, 4);
    EXPECT_EQ(lista.items[0].type, BT_DEV_TYPE_HEADPHONE);
    EXPECT_EQ(lista.items[1].type, BT_DEV_TYPE_MOUSE);
    EXPECT_EQ(lista.items[2].type, BT_DEV_TYPE_GENERIC);
    EXPECT_EQ(lista.items[3].type, BT_DEV_TYPE_GENERIC);
    /* Secao sem campos assume pareada com auto-conexao */
    EXPECT_TRUE(lista.items[3].paired);
    EXPECT_TRUE(lista.items[3].auto_connect);
}

TEST_F(BtStorageTest, CacheFallbackQuandoArquivoDesaparece)
{
    bt_saved_list_t base = {};
    ASSERT_EQ(bt_storage_save_all(&base), ESP_OK);

    bt_saved_device_t dev = dispositivo("AA:00:00:00:00:09", "Persistente", BT_DEV_TYPE_GENERIC);
    ASSERT_EQ(bt_storage_add_or_update(&dev), ESP_OK);

    hostmock::unlink_files({BT_CFG_PATH});

    bt_saved_list_t lista = {};
    ASSERT_EQ(bt_storage_load_all(&lista), ESP_OK);
    ASSERT_EQ(lista.count, 1);
    EXPECT_STREQ(lista.items[0].mac, "AA:00:00:00:00:09");
}

TEST_F(BtStorageTest, ListaCheiaRejeitaNovoDispositivo)
{
    bt_saved_list_t base = {};
    ASSERT_EQ(bt_storage_save_all(&base), ESP_OK);

    char mac[18];
    for (int i = 0; i < BT_MAX_SAVED_DEVICES; ++i) {
        snprintf(mac, sizeof(mac), "AA:00:00:00:%02X:%02X", i, i);
        bt_saved_device_t dev = dispositivo(mac, "Dev", BT_DEV_TYPE_GENERIC);
        ASSERT_EQ(bt_storage_add_or_update(&dev), ESP_OK);
    }

    bt_saved_device_t extra = dispositivo("AA:00:00:99:99:99", "Extra", BT_DEV_TYPE_GENERIC);
    EXPECT_EQ(bt_storage_add_or_update(&extra), ESP_ERR_NO_MEM);

    bt_saved_list_t do_arquivo = {};
    ASSERT_EQ(bt_storage_load_all(&do_arquivo), ESP_OK);
    EXPECT_EQ(do_arquivo.count, BT_MAX_SAVED_DEVICES);
}

TEST_F(BtStorageTest, ArgumentosInvalidos)
{
    bt_saved_list_t lista = {};
    EXPECT_EQ(bt_storage_load_all(nullptr), ESP_ERR_INVALID_ARG);
    EXPECT_EQ(bt_storage_save_all(nullptr), ESP_ERR_INVALID_ARG);
    EXPECT_EQ(bt_storage_load_all(&lista), ESP_ERR_NOT_FOUND); /* sem arquivo e sem cache */

    bt_saved_device_t vazio = {};
    EXPECT_EQ(bt_storage_add_or_update(nullptr), ESP_ERR_INVALID_ARG);
    EXPECT_EQ(bt_storage_add_or_update(&vazio), ESP_ERR_INVALID_ARG);

    EXPECT_EQ(bt_storage_remove(nullptr), ESP_ERR_INVALID_ARG);
    EXPECT_EQ(bt_storage_remove(""), ESP_ERR_INVALID_ARG);

    EXPECT_FALSE(bt_storage_find(nullptr, nullptr));
    EXPECT_FALSE(bt_storage_find("", nullptr));
}

} // namespace
