#include "nvs.h"
#include "nvs_mock.hpp"
#include "path_redirect.hpp"
#include "timezone_mgr.h"

#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iterator>
#include <string>

#include <gtest/gtest.h>

namespace {

class TimezoneMgrTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        hostmock::nvs_reset();
        hostmock::unlink_files({TIMEZONE_CFG_PATH});
        unsetenv("TZ");
    }

    void TearDown() override
    {
        unsetenv("TZ");
    }

    static void escrever_cfg(const std::string &conteudo)
    {
        std::ofstream out(hostmock::host_of(TIMEZONE_CFG_PATH), std::ios::trunc);
        out << conteudo;
    }

    static std::string ler_cfg()
    {
        std::ifstream in(hostmock::host_of(TIMEZONE_CFG_PATH));
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
};

TEST_F(TimezoneMgrTest, FormatOffsetVariacoes)
{
    char buf[16] = {};
    timezone_mgr_format_offset(0, buf, sizeof(buf));
    EXPECT_STREQ(buf, "0");
    timezone_mgr_format_offset(7, buf, sizeof(buf));
    EXPECT_STREQ(buf, "+7");
    timezone_mgr_format_offset(-3, buf, sizeof(buf));
    EXPECT_STREQ(buf, "-3");
}

TEST_F(TimezoneMgrTest, InitSemPersistenciaUsaDefaultBrasilia)
{
    ASSERT_EQ(timezone_mgr_init(), ESP_OK);
    EXPECT_EQ(timezone_mgr_get_offset(), TIMEZONE_DEFAULT_OFFSET);
    EXPECT_STREQ(std::getenv("TZ"), "UTC+3");
}

TEST_F(TimezoneMgrTest, InitCarregaDoSdQuandoNvsVazio)
{
    escrever_cfg("timezone=5\n");
    ASSERT_EQ(timezone_mgr_init(), ESP_OK);
    EXPECT_EQ(timezone_mgr_get_offset(), 5);
    EXPECT_STREQ(std::getenv("TZ"), "UTC-5");
}

TEST_F(TimezoneMgrTest, InitPrefereNvsAoSd)
{
    hostmock::nvs_seed_i32("tab5", "tz_offset", -5);
    escrever_cfg("timezone=5\n");
    ASSERT_EQ(timezone_mgr_init(), ESP_OK);
    EXPECT_EQ(timezone_mgr_get_offset(), -5);
    EXPECT_STREQ(std::getenv("TZ"), "UTC+5");
}

TEST_F(TimezoneMgrTest, InitDescartaOffsetForaDaFaixa)
{
    hostmock::nvs_seed_i32("tab5", "tz_offset", 100);
    ASSERT_EQ(timezone_mgr_init(), ESP_OK);
    EXPECT_EQ(timezone_mgr_get_offset(), TIMEZONE_DEFAULT_OFFSET);
}

TEST_F(TimezoneMgrTest, SetOffsetPersisteNvsSdEAmbiente)
{
    ASSERT_EQ(timezone_mgr_set_offset(7), ESP_OK);
    EXPECT_EQ(timezone_mgr_get_offset(), 7);
    EXPECT_STREQ(std::getenv("TZ"), "UTC-7");

    int32_t salvo = 0;
    nvs_handle_t handle = 0;
    ASSERT_EQ(nvs_open("tab5", NVS_READONLY, &handle), ESP_OK);
    ASSERT_EQ(nvs_get_i32(handle, "tz_offset", &salvo), ESP_OK);
    nvs_close(handle);
    EXPECT_EQ(salvo, 7);

    EXPECT_NE(ler_cfg().find("timezone=7"), std::string::npos);

    /* Reinicializacao restaura o valor persistido */
    ASSERT_EQ(timezone_mgr_init(), ESP_OK);
    EXPECT_EQ(timezone_mgr_get_offset(), 7);
}

TEST_F(TimezoneMgrTest, SetOffsetLimitaNosExtremos)
{
    ASSERT_EQ(timezone_mgr_set_offset(-99), ESP_OK);
    EXPECT_EQ(timezone_mgr_get_offset(), TIMEZONE_MIN_OFFSET);
    EXPECT_STREQ(std::getenv("TZ"), "UTC+12");

    ASSERT_EQ(timezone_mgr_set_offset(99), ESP_OK);
    EXPECT_EQ(timezone_mgr_get_offset(), TIMEZONE_MAX_OFFSET);
    EXPECT_STREQ(std::getenv("TZ"), "UTC-14");
}

TEST_F(TimezoneMgrTest, SetOffsetZeroUsaFormatoUtc0)
{
    ASSERT_EQ(timezone_mgr_set_offset(0), ESP_OK);
    EXPECT_STREQ(std::getenv("TZ"), "UTC0");
}

TEST_F(TimezoneMgrTest, GetLocaltimePreencheStructENullSeguro)
{
    ASSERT_EQ(timezone_mgr_set_offset(0), ESP_OK);
    struct tm info = {};
    ASSERT_NE(timezone_mgr_get_localtime(&info), nullptr);
    EXPECT_GE(info.tm_year, 120); /* ano >= 2020 */
    EXPECT_EQ(timezone_mgr_get_localtime(nullptr), nullptr);
}

} // namespace
