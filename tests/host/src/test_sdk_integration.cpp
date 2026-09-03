/**
 * @file test_sdk_integration.cpp
 * @brief Testes de Integração e Validação de Pacotes do Tab5 App SDK
 */

#include <gtest/gtest.h>
#include "tab5_manifest.h"
#include "tab5_package_mgr.h"
#include "app_registry.h"
#include "path_redirect.hpp"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

class SdkIntegrationTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        app_registry_init();
        tab5_package_mgr_init();
    }

    void TearDown() override
    {
        tab5_package_mgr_close_active();
    }
};

static std::string find_sdk_file(const char *rel_path)
{
    const char *prefixes[] = {"", "../../", "../", "../../../"};
    for (const char *p : prefixes) {
        std::string candidate = std::string(p) + rel_path;
        FILE *f = fopen(candidate.c_str(), "r");
        if (f != nullptr) {
            fclose(f);
            return candidate;
        }
    }
    return "";
}

TEST_F(SdkIntegrationTest, ValidateHelloAppTemplateManifest)
{
    tab5_manifest_t manifest = {};
    std::string manifest_path = find_sdk_file("sdk/tab5-app-sdk/templates/hello_app/manifest.json");
    ASSERT_FALSE(manifest_path.empty()) << "Arquivo de template manifest.json nao encontrado!";

    FILE *f = fopen(manifest_path.c_str(), "r");
    ASSERT_NE(f, nullptr);

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::vector<char> buf(sz + 1, 0);
    fread(buf.data(), 1, sz, f);
    fclose(f);

    EXPECT_EQ(tab5_manifest_parse_json(buf.data(), &manifest), TAB5_OK);
    EXPECT_TRUE(tab5_manifest_is_valid(&manifest));
    EXPECT_STREQ(manifest.id, "com.tab5.hello");
    EXPECT_STREQ(manifest.name, "Hello Tab5");
    EXPECT_STREQ(manifest.version, "1.0.0");
    EXPECT_STREQ(manifest.entry, "app.wasm");
    EXPECT_TRUE(manifest.permissions & TAB5_PERM_UI_KEYBOARD);
}

TEST_F(SdkIntegrationTest, ValidateNotesWasmExampleManifest)
{
    tab5_manifest_t manifest = {};
    std::string manifest_path = find_sdk_file("sdk/tab5-app-sdk/examples/notes_wasm/manifest.json");
    ASSERT_FALSE(manifest_path.empty()) << "Arquivo de exemplo notes_wasm manifest.json nao encontrado!";

    FILE *f = fopen(manifest_path.c_str(), "r");
    ASSERT_NE(f, nullptr);

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::vector<char> buf(sz + 1, 0);
    fread(buf.data(), 1, sz, f);
    fclose(f);

    EXPECT_EQ(tab5_manifest_parse_json(buf.data(), &manifest), TAB5_OK);
    EXPECT_TRUE(tab5_manifest_is_valid(&manifest));
    EXPECT_STREQ(manifest.id, "com.tab5.example.notes");
    EXPECT_STREQ(manifest.name, "Notas Wasm");
    EXPECT_EQ(manifest.file_assoc_count, 2);
    EXPECT_STREQ(manifest.file_associations[0], ".txt");
    EXPECT_STREQ(manifest.file_associations[1], ".md");
    EXPECT_TRUE(manifest.permissions & TAB5_PERM_STORAGE_READ);
    EXPECT_TRUE(manifest.permissions & TAB5_PERM_STORAGE_WRITE);
    EXPECT_TRUE(manifest.permissions & TAB5_PERM_UI_KEYBOARD);
}

TEST_F(SdkIntegrationTest, PackageInstallationFromSdkTemplate)
{
    std::string tmp_pkg = hostmock::tmp_root() + "/hello_packaged.tab5pkg";
    mkdir(tmp_pkg.c_str(), 0755);

    // Carrega o manifest do template
    std::string template_path = find_sdk_file("sdk/tab5-app-sdk/templates/hello_app/manifest.json");
    ASSERT_FALSE(template_path.empty());
    FILE *src_man = fopen(template_path.c_str(), "r");
    ASSERT_NE(src_man, nullptr);
    fseek(src_man, 0, SEEK_END);
    long sz = ftell(src_man);
    fseek(src_man, 0, SEEK_SET);
    std::vector<char> man_buf(sz + 1, 0);
    fread(man_buf.data(), 1, sz, src_man);
    fclose(src_man);

    // Escreve no pacote
    FILE *dst_man = fopen((tmp_pkg + "/manifest.json").c_str(), "w");
    ASSERT_NE(dst_man, nullptr);
    fwrite(man_buf.data(), 1, sz, dst_man);
    fclose(dst_man);

    // Escreve binário WASM fictício válido
    FILE *dst_wasm = fopen((tmp_pkg + "/app.wasm").c_str(), "wb");
    ASSERT_NE(dst_wasm, nullptr);
    uint8_t wasm_hdr[] = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    fwrite(wasm_hdr, 1, sizeof(wasm_hdr), dst_wasm);
    fclose(dst_wasm);

    // Instala
    char installed_id[64] = {0};
    EXPECT_EQ(tab5_package_mgr_install(tmp_pkg.c_str(), installed_id, sizeof(installed_id)), TAB5_OK);
    EXPECT_STREQ(installed_id, "com.tab5.hello");

    // Verifica no registro
    const app_desc_t *desc = app_registry_find_by_id("com.tab5.hello");
    ASSERT_NE(desc, nullptr);
    EXPECT_STREQ(desc->name, "Hello Tab5");

    // Launch e cleanup
    EXPECT_EQ(tab5_package_mgr_launch("com.tab5.hello", nullptr), TAB5_OK);
    EXPECT_EQ(tab5_package_mgr_close_active(), TAB5_OK);
    EXPECT_EQ(tab5_package_mgr_uninstall("com.tab5.hello", true), TAB5_OK);
}
