#include "nvs_mock.hpp"
#include "path_redirect.hpp"

#include <cerrno>
#include <fcntl.h>
#include <fstream>

#include "esp_err.h"
#include "nvs.h"
#include <iterator>
#include <string>

#include <gtest/gtest.h>

/* Testes de sanidade da propria infraestrutura: garantem que o --wrap
 * redireciona /sdcard para o tmpdir e que o NVS mockado se comporta
 * como os modulos de producao esperam. */

TEST(HostInfraTest, WrapFopenRedirecionaSdcardParaTmpdir)
{
    const std::string caminho_virtual = "/sdcard/probe.txt";

    FILE *arquivo = fopen(caminho_virtual.c_str(), "w");
    ASSERT_NE(arquivo, nullptr);
    fputs("tab5", arquivo);
    fclose(arquivo);

    std::ifstream entrada(hostmock::host_of(caminho_virtual));
    const std::string conteudo((std::istreambuf_iterator<char>(entrada)), std::istreambuf_iterator<char>());
    EXPECT_EQ(conteudo, "tab5");
}

TEST(HostInfraTest, WrapMkdirCriaDiretoriosDentroDoTmpdir)
{
    /* O tmproot ja provisiona tab5_os; recriar deve falhar apenas com EEXIST */
    const int rc = mkdir("/sdcard/tab5_os", 0755);
    ASSERT_TRUE(rc == 0 || errno == EEXIST);
    std::ifstream entrada(hostmock::host_of("/sdcard/tab5_os"));
    EXPECT_TRUE(entrada.good());
}

TEST(HostInfraTest, NvsReadonlyEmNamespaceInexistenteFalha)
{
    hostmock::nvs_reset();
    nvs_handle_t handle = 0;
    EXPECT_EQ(nvs_open("fantasma", NVS_READONLY, &handle), ESP_ERR_NOT_FOUND);
}

TEST(HostInfraTest, NvsRoundTripI32EU8)
{
    hostmock::nvs_reset();

    nvs_handle_t handle = 0;
    ASSERT_EQ(nvs_open("tab5", NVS_READWRITE, &handle), ESP_OK);
    ASSERT_EQ(nvs_set_i32(handle, "tz_offset", -3), ESP_OK);
    ASSERT_EQ(nvs_set_u8(handle, "brightness", 55), ESP_OK);
    ASSERT_EQ(nvs_commit(handle), ESP_OK);
    nvs_close(handle);

    int32_t offset = 0;
    uint8_t brilho = 0;
    ASSERT_EQ(nvs_open("tab5", NVS_READONLY, &handle), ESP_OK);
    ASSERT_EQ(nvs_get_i32(handle, "tz_offset", &offset), ESP_OK);
    ASSERT_EQ(nvs_get_u8(handle, "brightness", &brilho), ESP_OK);
    nvs_close(handle);
    EXPECT_EQ(offset, -3);
    EXPECT_EQ(brilho, 55);
}
