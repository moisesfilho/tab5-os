#include "display_storage.h"
#include "lvgl.h"
#include "nvs_mock.hpp"
#include "path_redirect.hpp"

#include <fstream>
#include <iterator>
#include <string>

#include <gtest/gtest.h>

namespace {

class DisplayStorageTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        hostmock::nvs_reset();
        hostmock::unlink_files({DISPLAY_CFG_PATH});
    }

    static void escrever_cfg(const std::string &conteudo)
    {
        std::ofstream out(hostmock::host_of(DISPLAY_CFG_PATH), std::ios::trunc);
        out << conteudo;
    }

    static std::string ler_cfg()
    {
        std::ifstream in(hostmock::host_of(DISPLAY_CFG_PATH));
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
};

TEST_F(DisplayStorageTest, LoadRotationNullRetornaInvalidArg)
{
    EXPECT_EQ(display_storage_load_rotation(nullptr), ESP_ERR_INVALID_ARG);
}

TEST_F(DisplayStorageTest, RotationSaveLoadRoundTrip)
{
    ASSERT_EQ(display_storage_save_rotation(LV_DISPLAY_ROTATION_180), ESP_OK);

    lv_disp_rotation_t rot = LV_DISPLAY_ROTATION_0;
    ASSERT_EQ(display_storage_load_rotation(&rot), ESP_OK);
    EXPECT_EQ(rot, LV_DISPLAY_ROTATION_180);
}

TEST_F(DisplayStorageTest, RotationSemArquivoRetornaNotFound)
{
    lv_disp_rotation_t rot = LV_DISPLAY_ROTATION_0;
    EXPECT_EQ(display_storage_load_rotation(&rot), ESP_ERR_NOT_FOUND);
}

TEST_F(DisplayStorageTest, SyncPreservaRotacaoEBrilhoNoMesmoCfg)
{
    ASSERT_EQ(display_storage_save_rotation(LV_DISPLAY_ROTATION_90), ESP_OK);
    ASSERT_EQ(display_storage_save_brightness(70), ESP_OK);

    const std::string conteudo = ler_cfg();
    EXPECT_NE(conteudo.find("rotation=1"), std::string::npos);
    EXPECT_NE(conteudo.find("brightness=70"), std::string::npos);

    lv_disp_rotation_t rot = LV_DISPLAY_ROTATION_0;
    ASSERT_EQ(display_storage_load_rotation(&rot), ESP_OK);
    EXPECT_EQ(rot, LV_DISPLAY_ROTATION_90);

    int brilho = 0;
    ASSERT_EQ(display_storage_load_brightness(&brilho), ESP_OK);
    EXPECT_EQ(brilho, 70);
}

TEST_F(DisplayStorageTest, BrightnessDefaultOitentaSemPersistencia)
{
    int brilho = 0;
    ASSERT_EQ(display_storage_load_brightness(&brilho), ESP_OK);
    EXPECT_EQ(brilho, DISPLAY_DEFAULT_BRIGHTNESS);
}

TEST_F(DisplayStorageTest, BrightnessPrefereNvs)
{
    hostmock::nvs_seed_u8("tab5", "brightness", 55);
    int brilho = 0;
    ASSERT_EQ(display_storage_load_brightness(&brilho), ESP_OK);
    EXPECT_EQ(brilho, 55);
}

TEST_F(DisplayStorageTest, BrightnessNvsForaDaFaixaUsaSdValido)
{
    hostmock::nvs_seed_u8("tab5", "brightness", 5);
    escrever_cfg("brightness=42\n");
    int brilho = 0;
    ASSERT_EQ(display_storage_load_brightness(&brilho), ESP_OK);
    EXPECT_EQ(brilho, 42);

    /* Sem SD valido, cai no default */
    hostmock::unlink_files({DISPLAY_CFG_PATH});
    ASSERT_EQ(display_storage_load_brightness(&brilho), ESP_OK);
    EXPECT_EQ(brilho, DISPLAY_DEFAULT_BRIGHTNESS);
}

TEST_F(DisplayStorageTest, BrightnessFallbackSdQuandoNvsVazio)
{
    escrever_cfg("rotation=2\nbrightness=42\n");
    int brilho = 0;
    ASSERT_EQ(display_storage_load_brightness(&brilho), ESP_OK);
    EXPECT_EQ(brilho, 42);
}

TEST_F(DisplayStorageTest, BrightnessSalvoComClampNosLimites)
{
    ASSERT_EQ(display_storage_save_brightness(3), ESP_OK);
    uint8_t no_nvs = 0;
    ASSERT_TRUE(hostmock::nvs_read_u8("tab5", "brightness", &no_nvs));
    EXPECT_EQ(no_nvs, DISPLAY_MIN_BRIGHTNESS);

    ASSERT_EQ(display_storage_save_brightness(255), ESP_OK);
    ASSERT_TRUE(hostmock::nvs_read_u8("tab5", "brightness", &no_nvs));
    EXPECT_EQ(no_nvs, DISPLAY_MAX_BRIGHTNESS);
}

TEST_F(DisplayStorageTest, ArgumentosInvalidos)
{
    EXPECT_EQ(display_storage_load_brightness(nullptr), ESP_ERR_INVALID_ARG);
}

} // namespace
