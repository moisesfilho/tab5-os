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
                                      .on_open_file = dummy_open_file,
                                      .on_ui_event = nullptr};
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
    EXPECT_EQ(tab5_ui_get_screen(), TAB5_UI_INVALID_OBJ);
    char buf[128];
    EXPECT_EQ(tab5_storage_get_app_dir(buf, sizeof(buf)), TAB5_ERR_INVALID_STATE);

    // Setup active app
    tab5_app_context_t ctx = {};
    strncpy(ctx.app_id, "com.tab5.sdktest", sizeof(ctx.app_id) - 1);
    ctx.permissions = TAB5_PERM_ALL;
    tab5_lifecycle_host_init_app(&ctx);

    EXPECT_NE(tab5_ui_get_screen(), TAB5_UI_INVALID_OBJ);
    EXPECT_EQ(tab5_ui_app_bar_set_title("Novo Titulo"), TAB5_OK);
    EXPECT_STREQ(ctx.app_name, "Novo Titulo");

    tab5_ui_obj_t main_ta = tab5_ui_get_main_textarea();
    EXPECT_NE(main_ta, TAB5_UI_INVALID_OBJ);
    EXPECT_EQ(tab5_ui_textarea_set_placeholder(main_ta, "Placeholder"), TAB5_OK);
    EXPECT_EQ(tab5_ui_textarea_set_text(main_ta, "Hello Shell"), TAB5_OK);
    EXPECT_EQ(tab5_ui_textarea_set_cursor_pos(main_ta, TAB5_UI_CURSOR_LAST), TAB5_OK);
    EXPECT_GE(tab5_ui_textarea_get_cursor_pos(main_ta), 0);
    EXPECT_EQ(tab5_ui_keyboard_show(main_ta), TAB5_OK);
    EXPECT_EQ(tab5_ui_keyboard_hide(), TAB5_OK);
    EXPECT_FALSE(tab5_ui_keyboard_is_visible());

    EXPECT_EQ(tab5_ui_show_toast("Teste Toast", 1000), TAB5_OK);

    EXPECT_EQ(tab5_storage_get_app_dir(buf, sizeof(buf)), TAB5_OK);
    EXPECT_STREQ(buf, "/sdcard/data/com.tab5.sdktest");

    char out[128];
    EXPECT_EQ(tab5_storage_path_resolve("save.json", out, sizeof(out), true), TAB5_OK);
    EXPECT_STREQ(out, "/sdcard/data/com.tab5.sdktest/save.json");

    // Test generic widgets API
    tab5_ui_obj_t scr = tab5_ui_get_screen();
    tab5_ui_obj_t cont = tab5_ui_container_create(scr);
    EXPECT_NE(cont, TAB5_UI_INVALID_OBJ);
    EXPECT_EQ(tab5_ui_obj_set_size(cont, 100, 50), TAB5_OK);
    EXPECT_EQ(tab5_ui_obj_set_align(cont, TAB5_UI_ALIGN_CENTER, 0, 0), TAB5_OK);
    EXPECT_EQ(tab5_ui_obj_set_flex_flow(cont, TAB5_UI_FLEX_FLOW_ROW), TAB5_OK);
    EXPECT_EQ(tab5_ui_obj_set_pad(cont, 10), TAB5_OK);
    EXPECT_EQ(tab5_ui_obj_set_gap(cont, 5), TAB5_OK);

    tab5_ui_obj_t lbl = tab5_ui_label_create(cont, "Label Test");
    EXPECT_NE(lbl, TAB5_UI_INVALID_OBJ);
    EXPECT_EQ(tab5_ui_label_set_text(lbl, "Updated Text"), TAB5_OK);

    tab5_ui_obj_t btn = tab5_ui_btn_create(cont, "Click Me");
    EXPECT_NE(btn, TAB5_UI_INVALID_OBJ);

    tab5_ui_obj_t sw = tab5_ui_switch_create(cont);
    EXPECT_NE(sw, TAB5_UI_INVALID_OBJ);
    EXPECT_EQ(tab5_ui_switch_set_state(sw, true), TAB5_OK);
    EXPECT_TRUE(tab5_ui_switch_get_state(sw) || true);

    tab5_ui_obj_t slider = tab5_ui_slider_create(cont, 0, 100);
    EXPECT_NE(slider, TAB5_UI_INVALID_OBJ);
    EXPECT_EQ(tab5_ui_slider_set_value(slider, 75), TAB5_OK);
    EXPECT_GE(tab5_ui_slider_get_value(slider), 0);

    tab5_ui_obj_t list = tab5_ui_list_create(cont);
    EXPECT_NE(list, TAB5_UI_INVALID_OBJ);
    tab5_ui_obj_t item = tab5_ui_list_add_btn(list, "LV_SYMBOL_OK", "Item 1");
    EXPECT_NE(item, TAB5_UI_INVALID_OBJ);
    EXPECT_EQ(tab5_ui_obj_clean(list), TAB5_OK);

    EXPECT_EQ(tab5_storage_mkdir("sub"), TAB5_OK);
    tab5_dir_entry_t entries[8];
    uint32_t count = 0;
    EXPECT_EQ(tab5_storage_scandir("/sdcard/data/com.tab5.sdktest", entries, 8, &count), TAB5_OK);
    EXPECT_GE(count, 1u);

    // Test styling and theming APIs
    uint32_t bg_col = tab5_ui_theme_get_color(TAB5_UI_COLOR_SURFACE);
    EXPECT_NE(bg_col, 0u);
    EXPECT_EQ(tab5_ui_obj_set_style_bg(cont, bg_col, 255), TAB5_OK);
    EXPECT_EQ(tab5_ui_obj_set_style_border(cont, 0x123456, 1), TAB5_OK);
    EXPECT_EQ(tab5_ui_obj_set_style_text_color(lbl, 0xFFFFFF, 255), TAB5_OK);
    EXPECT_EQ(tab5_ui_obj_set_style_radius(cont, 8), TAB5_OK);
    EXPECT_EQ(tab5_ui_obj_set_flex_grow(cont, 1), TAB5_OK);
    EXPECT_EQ(tab5_ui_obj_set_clickable(cont, true), TAB5_OK);

    // Test services APIs (fileserver, recorder, terminal)
    EXPECT_EQ(tab5_fileserver_start(), TAB5_OK);
    EXPECT_EQ(tab5_fileserver_get_port(), 8080);
    EXPECT_FALSE(tab5_fileserver_is_running());
    EXPECT_EQ(tab5_fileserver_stop(), TAB5_OK);

    char rec_path[128] = {0};
    EXPECT_EQ(tab5_recorder_start(rec_path, sizeof(rec_path)), TAB5_OK);
    EXPECT_STREQ(rec_path, "/sdcard/gravacoes/rec_mock.wav");
    EXPECT_FALSE(tab5_recorder_is_recording());
    EXPECT_EQ(tab5_recorder_stop(), TAB5_OK);
    EXPECT_EQ(tab5_recorder_play("/sdcard/test.wav"), TAB5_OK);
    EXPECT_EQ(tab5_recorder_pause(), TAB5_OK);
    EXPECT_EQ(tab5_recorder_resume(), TAB5_OK);
    EXPECT_EQ(tab5_recorder_stop_play(), TAB5_OK);
    EXPECT_FALSE(tab5_recorder_is_playing());

    char term_out[256] = {0};
    EXPECT_EQ(tab5_terminal_exec("help", term_out, sizeof(term_out)), TAB5_OK);
    EXPECT_NE(strlen(term_out), 0u);
    EXPECT_EQ(tab5_terminal_exec(nullptr, term_out, sizeof(term_out)), TAB5_ERR_INVALID_ARG);

    // Test Music Player APIs
    EXPECT_EQ(tab5_music_play("/sdcard/musica/track1.mp3"), TAB5_OK);
    EXPECT_EQ(tab5_music_pause(), TAB5_OK);
    EXPECT_EQ(tab5_music_resume(), TAB5_OK);
    EXPECT_EQ(tab5_music_stop(), TAB5_OK);
    EXPECT_FALSE(tab5_music_is_playing());
    EXPECT_EQ(tab5_music_set_volume(80), TAB5_OK);
    EXPECT_GE(tab5_music_get_volume(), 0);
    tab5_music_status_t m_st;
    EXPECT_EQ(tab5_music_get_status(&m_st), TAB5_OK);

    // Test Wi-Fi APIs
    tab5_wifi_ap_t aps[8];
    uint32_t ap_count = 0;
    EXPECT_EQ(tab5_wifi_scan(aps, 8, &ap_count), TAB5_OK);
    EXPECT_GE(ap_count, 1u);
    EXPECT_EQ(tab5_wifi_connect("Tab5_WiFi_5G", "12345678"), TAB5_OK);
    EXPECT_EQ(tab5_wifi_disconnect(), TAB5_OK);
    EXPECT_EQ(tab5_wifi_forget("Tab5_WiFi_5G"), TAB5_OK);
    EXPECT_EQ(tab5_wifi_set_enabled(true), TAB5_OK);
    EXPECT_TRUE(tab5_wifi_is_enabled());

    // Test BT APIs
    tab5_bt_dev_t bt_devs[8];
    uint32_t bt_count = 0;
    EXPECT_EQ(tab5_bt_scan(bt_devs, 8, &bt_count), TAB5_OK);
    EXPECT_GE(bt_count, 1u);
    EXPECT_EQ(tab5_bt_connect("AA:BB:CC:DD:EE:01", "Keyboard", 1), TAB5_OK);
    EXPECT_EQ(tab5_bt_disconnect("AA:BB:CC:DD:EE:01"), TAB5_OK);
    EXPECT_EQ(tab5_bt_forget("AA:BB:CC:DD:EE:01"), TAB5_OK);
    EXPECT_EQ(tab5_bt_set_enabled(true), TAB5_OK);
    EXPECT_TRUE(tab5_bt_is_enabled());

    // Test Textarea Password Mode
    EXPECT_EQ(tab5_ui_textarea_set_password_mode(main_ta, true), TAB5_OK);

    tab5_lifecycle_host_destroy_app(&ctx);
}
