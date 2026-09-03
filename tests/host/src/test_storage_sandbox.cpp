#include <gtest/gtest.h>
#include "tab5_storage_sandbox.h"
#include "tab5_host_abi.h"
#include <cstring>

TEST(StorageSandboxTest, GetAppDir)
{
    char buf[128];
    EXPECT_EQ(tab5_storage_sandbox_get_app_dir("com.tab5.notas", buf, sizeof(buf)), TAB5_OK);
    EXPECT_STREQ(buf, "/sdcard/data/com.tab5.notas");

    // Invalid args
    EXPECT_EQ(tab5_storage_sandbox_get_app_dir(nullptr, buf, sizeof(buf)), TAB5_ERR_INVALID_ARG);
    EXPECT_EQ(tab5_storage_sandbox_get_app_dir("", buf, sizeof(buf)), TAB5_ERR_INVALID_ARG);
    EXPECT_EQ(tab5_storage_sandbox_get_app_dir("com.tab5.notas", nullptr, sizeof(buf)), TAB5_ERR_INVALID_ARG);
    EXPECT_EQ(tab5_storage_sandbox_get_app_dir("com.tab5.notas", buf, 5), TAB5_ERR_BUFFER_OVERFLOW);
}

TEST(StorageSandboxTest, RelativePathResolvesInsideAppSandbox)
{
    char out[128];
    EXPECT_EQ(tab5_storage_sandbox_resolve_path("notes.txt", out, sizeof(out), "com.tab5.notas", TAB5_PERM_NONE, true),
              TAB5_OK);
    EXPECT_STREQ(out, "/sdcard/data/com.tab5.notas/notes.txt");

    EXPECT_EQ(tab5_storage_sandbox_resolve_path("sub/folder/file.json", out, sizeof(out), "com.tab5.notas",
                                                TAB5_PERM_NONE, false),
              TAB5_OK);
    EXPECT_STREQ(out, "/sdcard/data/com.tab5.notas/sub/folder/file.json");
}

TEST(StorageSandboxTest, AbsolutePathInsideSandboxAllowed)
{
    char out[128];
    EXPECT_EQ(tab5_storage_sandbox_resolve_path("/sdcard/data/com.tab5.notas/doc.md", out, sizeof(out),
                                                "com.tab5.notas", TAB5_PERM_NONE, true),
              TAB5_OK);
    EXPECT_STREQ(out, "/sdcard/data/com.tab5.notas/doc.md");
}

TEST(StorageSandboxTest, InstalledAssetsReadOnly)
{
    char out[128];
    // Read is allowed
    EXPECT_EQ(tab5_storage_sandbox_resolve_path("/sdcard/apps/installed/com.tab5.notas/assets/icon.png", out,
                                                sizeof(out), "com.tab5.notas", TAB5_PERM_NONE, false),
              TAB5_OK);
    EXPECT_STREQ(out, "/sdcard/apps/installed/com.tab5.notas/assets/icon.png");

    // Write is blocked
    EXPECT_EQ(tab5_storage_sandbox_resolve_path("/sdcard/apps/installed/com.tab5.notas/assets/icon.png", out,
                                                sizeof(out), "com.tab5.notas", TAB5_PERM_ALL, true),
              TAB5_ERR_ACCESS_DENIED);
}

TEST(StorageSandboxTest, OtherAppDataAndBinariesBlocked)
{
    char out[128];
    // Access to other app data blocked
    EXPECT_EQ(tab5_storage_sandbox_resolve_path("/sdcard/data/com.other.app/secret.db", out, sizeof(out),
                                                "com.tab5.notas", TAB5_PERM_ALL, false),
              TAB5_ERR_ACCESS_DENIED);

    // Access to other app installed binary blocked
    EXPECT_EQ(tab5_storage_sandbox_resolve_path("/sdcard/apps/installed/com.other.app/app.wasm", out, sizeof(out),
                                                "com.tab5.notas", TAB5_PERM_ALL, false),
              TAB5_ERR_ACCESS_DENIED);
}

TEST(StorageSandboxTest, SdSharedPermissionsCheck)
{
    char out[128];
    const char *shared_file = "/sdcard/shared.txt";

    // Without permissions
    EXPECT_EQ(tab5_storage_sandbox_resolve_path(shared_file, out, sizeof(out), "com.tab5.notas", TAB5_PERM_NONE, false),
              TAB5_ERR_ACCESS_DENIED);
    EXPECT_EQ(tab5_storage_sandbox_resolve_path(shared_file, out, sizeof(out), "com.tab5.notas", TAB5_PERM_NONE, true),
              TAB5_ERR_ACCESS_DENIED);

    // With STORAGE_READ
    EXPECT_EQ(tab5_storage_sandbox_resolve_path(shared_file, out, sizeof(out), "com.tab5.notas", TAB5_PERM_STORAGE_READ,
                                                false),
              TAB5_OK);
    EXPECT_STREQ(out, "/sdcard/shared.txt");
    EXPECT_EQ(tab5_storage_sandbox_resolve_path(shared_file, out, sizeof(out), "com.tab5.notas", TAB5_PERM_STORAGE_READ,
                                                true),
              TAB5_ERR_ACCESS_DENIED);

    // With STORAGE_WRITE
    EXPECT_EQ(tab5_storage_sandbox_resolve_path(shared_file, out, sizeof(out), "com.tab5.notas",
                                                TAB5_PERM_STORAGE_WRITE, true),
              TAB5_OK);
    EXPECT_STREQ(out, "/sdcard/shared.txt");
}

TEST(StorageSandboxTest, PathTraversalBlocked)
{
    char out[128];
    // Path traversal trying to escape app data directory
    EXPECT_EQ(tab5_storage_sandbox_resolve_path("../other_app/data.txt", out, sizeof(out), "com.tab5.notas",
                                                TAB5_PERM_NONE, false),
              TAB5_ERR_ACCESS_DENIED);

    EXPECT_EQ(tab5_storage_sandbox_resolve_path("sub/../../../../nvs/secret", out, sizeof(out), "com.tab5.notas",
                                                TAB5_PERM_ALL, false),
              TAB5_ERR_ACCESS_DENIED);

    EXPECT_EQ(tab5_storage_sandbox_resolve_path("/sdcard/data/com.tab5.notas/../../../nvs/keys", out, sizeof(out),
                                                "com.tab5.notas", TAB5_PERM_ALL, false),
              TAB5_ERR_ACCESS_DENIED);
}

TEST(StorageSandboxTest, NonSdPathsBlocked)
{
    char out[128];
    EXPECT_EQ(tab5_storage_sandbox_resolve_path("/nvs/wifi", out, sizeof(out), "com.tab5.notas", TAB5_PERM_ALL, false),
              TAB5_ERR_ACCESS_DENIED);
    EXPECT_EQ(
        tab5_storage_sandbox_resolve_path("/spiffs/sys.bin", out, sizeof(out), "com.tab5.notas", TAB5_PERM_ALL, false),
        TAB5_ERR_ACCESS_DENIED);
    EXPECT_EQ(
        tab5_storage_sandbox_resolve_path("/etc/passwd", out, sizeof(out), "com.tab5.notas", TAB5_PERM_ALL, false),
        TAB5_ERR_ACCESS_DENIED);
}
