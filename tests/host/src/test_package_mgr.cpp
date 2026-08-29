#include <gtest/gtest.h>
#include "tab5_package_mgr.h"
#include "app_registry.h"
#include "path_redirect.hpp"
#include <cstring>
#include <cstdio>
#include <sys/stat.h>

class PackageMgrTest : public ::testing::Test {
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

TEST_F(PackageMgrTest, InstallLaunchAndUninstallApp)
{
    // 1. Cria uma pasta temporária de pacote de app
    std::string tmp_pkg = hostmock::tmp_root() + "/source_sample_app";
    mkdir(tmp_pkg.c_str(), 0755);

    std::string manifest_path = tmp_pkg + "/manifest.json";
    FILE *f_man = fopen(manifest_path.c_str(), "w");
    ASSERT_NE(f_man, nullptr);
    const char *manifest_json = R"({
        "id": "com.tab5.sample",
        "name": "Sample App",
        "version": "1.0.0",
        "entry": "app.wasm",
        "permissions": ["storage.readwrite", "ui.keyboard"]
    })";
    fwrite(manifest_json, 1, strlen(manifest_json), f_man);
    fclose(f_man);

    std::string wasm_path = tmp_pkg + "/app.wasm";
    FILE *f_wasm = fopen(wasm_path.c_str(), "wb");
    ASSERT_NE(f_wasm, nullptr);
    uint8_t wasm_magic[] = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    fwrite(wasm_magic, 1, sizeof(wasm_magic), f_wasm);
    fclose(f_wasm);

    // 2. Instalação
    char installed_id[64] = {0};
    EXPECT_EQ(tab5_package_mgr_install(tmp_pkg.c_str(), installed_id, sizeof(installed_id)), TAB5_OK);
    EXPECT_STREQ(installed_id, "com.tab5.sample");

    // Verifica se foi registrado no app_registry
    const app_desc_t *desc = app_registry_find_by_id("com.tab5.sample");
    ASSERT_NE(desc, nullptr);
    EXPECT_STREQ(desc->name, "Sample App");

    // 3. Consulta de informações
    tab5_installed_app_info_t info = {};
    EXPECT_EQ(tab5_package_mgr_get_app_info("com.tab5.sample", &info), TAB5_OK);
    EXPECT_STREQ(info.manifest.name, "Sample App");

    // 4. Execução
    EXPECT_EQ(tab5_package_mgr_launch("com.tab5.sample", nullptr), TAB5_OK);
    EXPECT_EQ(tab5_package_mgr_close_active(), TAB5_OK);

    // 5. Desinstalação
    EXPECT_EQ(tab5_package_mgr_uninstall("com.tab5.sample", true), TAB5_OK);
    EXPECT_EQ(app_registry_find_by_id("com.tab5.sample"), nullptr);
}

TEST_F(PackageMgrTest, ScanInstalledApps)
{
    // Cria 2 apps instaladas manualmente
    std::string app1_dir = std::string(TAB5_APPS_INSTALLED_DIR) + "/com.tab5.app1";
    std::string app2_dir = std::string(TAB5_APPS_INSTALLED_DIR) + "/com.tab5.app2";
    mkdir(app1_dir.c_str(), 0755);
    mkdir(app2_dir.c_str(), 0755);

    FILE *f1 = fopen((app1_dir + "/manifest.json").c_str(), "w");
    if (f1) {
        fputs("{\"id\": \"com.tab5.app1\", \"name\": \"App 1\"}", f1);
        fclose(f1);
    }

    FILE *f2 = fopen((app2_dir + "/manifest.json").c_str(), "w");
    if (f2) {
        fputs("{\"id\": \"com.tab5.app2\", \"name\": \"App 2\"}", f2);
        fclose(f2);
    }

    int registered = tab5_package_mgr_scan_and_register_all();
    EXPECT_GE(registered, 2);
    EXPECT_NE(app_registry_find_by_id("com.tab5.app1"), nullptr);
    EXPECT_NE(app_registry_find_by_id("com.tab5.app2"), nullptr);

    tab5_package_mgr_uninstall("com.tab5.app1", false);
    tab5_package_mgr_uninstall("com.tab5.app2", false);
}
