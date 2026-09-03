#include <gtest/gtest.h>
#include "storage_mgr.h"
#include "tab5_package_mgr.h"
#include "app_registry.h"
#include "path_redirect.hpp"
#include <cstring>
#include <cstdio>
#include <sys/stat.h>

class StorageMgrTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        app_registry_init();
        tab5_package_mgr_init();
    }
};

TEST_F(StorageMgrTest, PathSizeCalculation)
{
    std::string test_dir = hostmock::tmp_root() + "/test_dir_size";
    mkdir(test_dir.c_str(), 0755);

    std::string f1_path = test_dir + "/file1.bin";
    FILE *f1 = fopen(f1_path.c_str(), "wb");
    ASSERT_NE(f1, nullptr);
    uint8_t dummy[1024] = {0};
    fwrite(dummy, 1, sizeof(dummy), f1);
    fclose(f1);

    std::string sub_dir = test_dir + "/sub";
    mkdir(sub_dir.c_str(), 0755);

    std::string f2_path = sub_dir + "/file2.bin";
    FILE *f2 = fopen(f2_path.c_str(), "wb");
    ASSERT_NE(f2, nullptr);
    fwrite(dummy, 1, 512, f2);
    fclose(f2);

    EXPECT_EQ(tab5_storage_mgr_calculate_path_size(f1_path.c_str()), 1024u);
    EXPECT_EQ(tab5_storage_mgr_calculate_path_size(f2_path.c_str()), 512u);
    EXPECT_GE(tab5_storage_mgr_calculate_path_size(test_dir.c_str()), 1536u);
}

TEST_F(StorageMgrTest, RamAndDiskStats)
{
    tab5_ram_stats_t ram = {};
    EXPECT_EQ(tab5_storage_mgr_get_ram_stats(&ram), TAB5_OK);
    EXPECT_GT(ram.internal_free_bytes, 0u);
    EXPECT_GT(ram.total_free_bytes, 0u);

    tab5_storage_stats_t flash = {};
    EXPECT_EQ(tab5_storage_mgr_get_flash_apps_stats(&flash), TAB5_OK);
    EXPECT_EQ(flash.total_bytes, 4u * 1024 * 1024);

    tab5_storage_stats_t sd = {};
    EXPECT_EQ(tab5_storage_mgr_get_sd_stats(&sd), TAB5_OK);
    EXPECT_GT(sd.total_bytes, 0u);
    EXPECT_GT(sd.free_bytes, 0u);
    EXPECT_TRUE(tab5_storage_mgr_has_enough_sd_space(1024));
}

TEST_F(StorageMgrTest, ListInstalledAppsAndPendingPackages)
{
    // 1. Cria app instalada no SD
    std::string sd_app = std::string(TAB5_APPS_INSTALLED_DIR) + "/com.tab5.demo";
    mkdir(sd_app.c_str(), 0755);

    FILE *f_man = fopen((sd_app + "/manifest.json").c_str(), "w");
    ASSERT_NE(f_man, nullptr);
    fputs("{\"id\": \"com.tab5.demo\", \"name\": \"Demo App\", \"version\": \"2.0.0\"}", f_man);
    fclose(f_man);

    // Cria arquivo de dados no sandbox
    std::string data_dir = std::string(TAB5_APPS_DATA_DIR) + "/com.tab5.demo";
    mkdir(data_dir.c_str(), 0755);
    FILE *f_data = fopen((data_dir + "/save.dat").c_str(), "wb");
    if (f_data) {
        uint8_t d[256] = {0};
        fwrite(d, 1, sizeof(d), f_data);
        fclose(f_data);
    }

    auto installed = tab5_storage_mgr_list_installed_apps();
    bool found_demo = false;
    for (const auto &it : installed) {
        if (strcmp(it.id, "com.tab5.demo") == 0) {
            found_demo = true;
            EXPECT_STREQ(it.name, "Demo App");
            EXPECT_STREQ(it.version, "2.0.0");
            EXPECT_FALSE(it.is_embedded);
            EXPECT_GE(it.data_size_bytes, 256u);
            break;
        }
    }
    EXPECT_TRUE(found_demo);

    // 2. Cria pacote pendente em /sdcard/apps/
    std::string pkg_dir = std::string(TAB5_APPS_DIR) + "/game.tab5pkg";
    mkdir(pkg_dir.c_str(), 0755);
    FILE *f_pkg_man = fopen((pkg_dir + "/manifest.json").c_str(), "w");
    ASSERT_NE(f_pkg_man, nullptr);
    fputs("{\"id\": \"com.tab5.game\", \"name\": \"Space Game\", \"version\": \"1.0.0\"}", f_pkg_man);
    fclose(f_pkg_man);

    auto pending = tab5_storage_mgr_list_pending_packages();
    bool found_pkg = false;
    for (const auto &p : pending) {
        if (strcmp(p.id, "com.tab5.game") == 0) {
            found_pkg = true;
            EXPECT_STREQ(p.name, "Space Game");
            break;
        }
    }
    EXPECT_TRUE(found_pkg);

    tab5_package_mgr_uninstall("com.tab5.demo", true);
}
