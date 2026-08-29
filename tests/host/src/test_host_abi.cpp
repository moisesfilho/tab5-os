#include <gtest/gtest.h>
#include "tab5_host_abi.h"
#include "tab5_lifecycle_host.h"
#include "tab5_ui_host.h"
#include "tab5_sys_host.h"
#include <cstring>

static bool s_init_called = false;
static bool s_resume_called = false;
static bool s_pause_called = false;
static bool s_destroy_called = false;
static std::string s_opened_file;

static void dummy_init(void)
{
    s_init_called = true;
}
static void dummy_resume(void)
{
    s_resume_called = true;
}
static void dummy_pause(void)
{
    s_pause_called = true;
}
static void dummy_destroy(void)
{
    s_destroy_called = true;
}
static void dummy_open_file(const char *path)
{
    s_opened_file = (path != nullptr ? path : "");
}

class HostAbiTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        s_init_called = false;
        s_resume_called = false;
        s_pause_called = false;
        s_destroy_called = false;
        s_opened_file.clear();
        tab5_host_abi_init();
    }

    void TearDown() override
    {
        tab5_host_clear_active_app();
    }
};

TEST_F(HostAbiTest, NativeSymbolsTableExported)
{
    uint32_t count = 0;
    const tab5_native_symbol_t *symbols = tab5_host_abi_get_symbols(&count);

    ASSERT_NE(symbols, nullptr);
    EXPECT_GT(count, 10u);

    bool found_screen = false;
    bool found_storage = false;
    bool found_battery = false;

    for (uint32_t i = 0; i < count; ++i) {
        if (strcmp(symbols[i].symbol_name, "tab5_ui_get_screen") == 0)
            found_screen = true;
        if (strcmp(symbols[i].symbol_name, "tab5_storage_path_resolve") == 0)
            found_storage = true;
        if (strcmp(symbols[i].symbol_name, "tab5_system_get_battery") == 0)
            found_battery = true;
        EXPECT_NE(symbols[i].func_ptr, nullptr);
        EXPECT_NE(symbols[i].signature, nullptr);
    }

    EXPECT_TRUE(found_screen);
    EXPECT_TRUE(found_storage);
    EXPECT_TRUE(found_battery);
}

TEST_F(HostAbiTest, ContextAndPermissionsManagement)
{
    EXPECT_EQ(tab5_host_get_active_app(), nullptr);
    EXPECT_FALSE(tab5_host_has_permission(TAB5_PERM_STORAGE_READ));

    tab5_app_context_t ctx = {};
    strncpy(ctx.app_id, "com.tab5.demo", sizeof(ctx.app_id) - 1);
    ctx.permissions = TAB5_PERM_STORAGE_READ | TAB5_PERM_UI_KEYBOARD;

    EXPECT_EQ(tab5_host_set_active_app(&ctx), TAB5_OK);
    EXPECT_EQ(tab5_host_get_active_app(), &ctx);
    EXPECT_TRUE(tab5_host_has_permission(TAB5_PERM_STORAGE_READ));
    EXPECT_TRUE(tab5_host_has_permission(TAB5_PERM_UI_KEYBOARD));
    EXPECT_FALSE(tab5_host_has_permission(TAB5_PERM_NETWORK));

    tab5_host_clear_active_app();
    EXPECT_EQ(tab5_host_get_active_app(), nullptr);
}

TEST_F(HostAbiTest, LifecycleTransitions)
{
    tab5_app_context_t ctx = {};
    strncpy(ctx.app_id, "com.tab5.testapp", sizeof(ctx.app_id) - 1);
    strncpy(ctx.app_name, "Test App", sizeof(ctx.app_name) - 1);
    ctx.permissions = TAB5_PERM_STORAGE_READ;
    ctx.state = TAB5_APP_STATE_UNINITIALIZED;

    tab5_lifecycle_callbacks_t cbs = {.on_init = dummy_init,
                                      .on_resume = dummy_resume,
                                      .on_pause = dummy_pause,
                                      .on_destroy = dummy_destroy,
                                      .on_open_file = dummy_open_file};
    ctx.lifecycle = cbs;

    // 1. Init
    EXPECT_EQ(tab5_lifecycle_host_init_app(&ctx), TAB5_OK);
    EXPECT_TRUE(s_init_called);
    EXPECT_EQ(ctx.state, TAB5_APP_STATE_INITIALIZED);
    EXPECT_NE(ctx.root_screen, nullptr);

    // Cannot re-init
    EXPECT_EQ(tab5_lifecycle_host_init_app(&ctx), TAB5_ERR_INVALID_STATE);

    // 2. Resume
    EXPECT_EQ(tab5_lifecycle_host_resume_app(&ctx), TAB5_OK);
    EXPECT_TRUE(s_resume_called);
    EXPECT_EQ(ctx.state, TAB5_APP_STATE_RESUMED);

    // 3. Open file (inside sandbox)
    EXPECT_EQ(tab5_lifecycle_host_open_file(&ctx, "test.txt"), TAB5_OK);
    EXPECT_EQ(s_opened_file, "/sdcard/data/com.tab5.testapp/test.txt");

    // Open file (forbidden path)
    EXPECT_EQ(tab5_lifecycle_host_open_file(&ctx, "/nvs/secret"), TAB5_ERR_ACCESS_DENIED);

    // 4. Pause
    EXPECT_EQ(tab5_lifecycle_host_pause_app(&ctx), TAB5_OK);
    EXPECT_TRUE(s_pause_called);
    EXPECT_EQ(ctx.state, TAB5_APP_STATE_PAUSED);

    // 5. Destroy
    EXPECT_EQ(tab5_lifecycle_host_destroy_app(&ctx), TAB5_OK);
    EXPECT_TRUE(s_destroy_called);
    EXPECT_EQ(ctx.state, TAB5_APP_STATE_DESTROYED);
    EXPECT_EQ(ctx.root_screen, nullptr);
    EXPECT_EQ(tab5_host_get_active_app(), nullptr);
}

TEST_F(HostAbiTest, SystemAndHardwareApis)
{
    tab5_battery_info_t bat = {};
    EXPECT_EQ(tab5_system_get_battery(&bat), TAB5_OK);
    EXPECT_GE(bat.percent, 0);
    EXPECT_LE(bat.percent, 100);

    tab5_wifi_info_t wifi = {};
    EXPECT_EQ(tab5_system_get_wifi_status(&wifi), TAB5_OK);

    tab5_bt_info_t bt = {};
    EXPECT_EQ(tab5_system_get_bt_status(&bt), TAB5_OK);

    int64_t epoch = 0;
    struct tm tm_info = {};
    EXPECT_EQ(tab5_system_get_time(&epoch, &tm_info), TAB5_OK);
    EXPECT_GT(epoch, 0);

    EXPECT_EQ(tab5_sound_play_beep(1000, 50), TAB5_OK);

    tab5_system_log(2, "TEST", "Message log test");
}

TEST_F(HostAbiTest, UiAndStorageSdkFunctions)
{
    // Calling SDK without active app returns error/null
    EXPECT_EQ(tab5_ui_get_screen(), nullptr);
    char buf[128];
    EXPECT_EQ(tab5_storage_get_app_dir(buf, sizeof(buf)), TAB5_ERR_INVALID_STATE);

    // Setup active app
    tab5_app_context_t ctx = {};
    strncpy(ctx.app_id, "com.tab5.sdktest", sizeof(ctx.app_id) - 1);
    ctx.permissions = TAB5_PERM_ALL;
    tab5_lifecycle_host_init_app(&ctx);

    EXPECT_NE(tab5_ui_get_screen(), nullptr);
    EXPECT_EQ(tab5_ui_app_bar_set_title("Novo Titulo"), TAB5_OK);
    EXPECT_STREQ(ctx.app_name, "Novo Titulo");

    EXPECT_EQ(tab5_ui_show_toast("Teste Toast", 1000), TAB5_OK);

    EXPECT_EQ(tab5_storage_get_app_dir(buf, sizeof(buf)), TAB5_OK);
    EXPECT_STREQ(buf, "/sdcard/data/com.tab5.sdktest");

    char out[128];
    EXPECT_EQ(tab5_storage_path_resolve("save.json", out, sizeof(out), true), TAB5_OK);
    EXPECT_STREQ(out, "/sdcard/data/com.tab5.sdktest/save.json");

    tab5_lifecycle_host_destroy_app(&ctx);
}
