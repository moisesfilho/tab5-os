#include "app_registry.h"
#include "file_assoc.h"

#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <atomic>
#include <utility>

namespace {

std::string g_txt_aberto;

void abrir_txt(const char *path)
{
    g_txt_aberto = path;
}

void lancar_app() {}

const char *kExtensoesTxt[] = {"txt", nullptr};

app_desc_t desc_base()
{
    app_desc_t desc = {};
    desc.id = "notas";
    desc.name = "Notas";
    desc.icon_symbol = "E";
    desc.on_launch = lancar_app;
    return desc;
}

class AppRegistryTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        app_registry_init();
        file_assoc_init();
        g_txt_aberto.clear();
    }
};

TEST_F(AppRegistryTest, RegisterValidaDescritor)
{
    app_desc_t desc = desc_base();
    EXPECT_EQ(app_registry_register(nullptr), ESP_ERR_INVALID_ARG);

    desc.id = nullptr;
    EXPECT_EQ(app_registry_register(&desc), ESP_ERR_INVALID_ARG);

    desc = desc_base();
    desc.name = nullptr;
    EXPECT_EQ(app_registry_register(&desc), ESP_ERR_INVALID_ARG);
}

TEST_F(AppRegistryTest, RegisterAdicionaEBuscaPorIdEIndice)
{
    app_desc_t desc = desc_base();
    ASSERT_EQ(app_registry_register(&desc), ESP_OK);
    EXPECT_EQ(app_registry_get_count(), 1);

    app_desc_t found = {};
    ASSERT_EQ(app_registry_find_by_id("notas", &found), ESP_OK);
    EXPECT_STREQ(found.name, "Notas");
    EXPECT_STREQ(found.icon_symbol, "E");
    app_registry_release_snapshot(&found);

    ASSERT_EQ(app_registry_get_by_index(0, &found), ESP_OK);
    EXPECT_STREQ(found.id, "notas");
    app_registry_release_snapshot(&found);

    EXPECT_EQ(app_registry_get_by_index(-1, &found), ESP_ERR_NOT_FOUND);
    EXPECT_EQ(app_registry_get_by_index(1, &found), ESP_ERR_NOT_FOUND);
    EXPECT_EQ(app_registry_find_by_id(nullptr, &found), ESP_ERR_INVALID_ARG);
    EXPECT_EQ(app_registry_find_by_id("inexistente", &found), ESP_ERR_NOT_FOUND);
    EXPECT_EQ(app_registry_get_all().size(), 1u);
}

TEST_F(AppRegistryTest, RegisterRejeitaIdDuplicado)
{
    app_desc_t desc = desc_base();
    ASSERT_EQ(app_registry_register(&desc), ESP_OK);

    app_desc_t outro = desc_base();
    EXPECT_EQ(app_registry_register(&outro), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(app_registry_get_count(), 1);
}

TEST_F(AppRegistryTest, RegistroAutoassociaExtensoes)
{
    app_desc_t desc = desc_base();
    desc.file_extensions = kExtensoesTxt;
    desc.on_open_file = abrir_txt;
    ASSERT_EQ(app_registry_register(&desc), ESP_OK);

    EXPECT_EQ(file_assoc_open("/sdcard/notas/a.txt"), ESP_OK);
    EXPECT_EQ(g_txt_aberto, "/sdcard/notas/a.txt");
}

TEST_F(AppRegistryTest, ExtensoesSemCallbackNaoSaoRegistradas)
{
    app_desc_t desc = desc_base();
    desc.file_extensions = kExtensoesTxt;
    ASSERT_EQ(app_registry_register(&desc), ESP_OK);
    EXPECT_EQ(file_assoc_open("a.txt"), ESP_ERR_NOT_FOUND);
}

TEST_F(AppRegistryTest, SnapshotsContinuamValidosDuranteRegistroConcorrente)
{
    app_desc_t base = desc_base();
    ASSERT_EQ(app_registry_register(&base), ESP_OK);
    std::atomic<bool> stop = false;
    std::thread reader([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            const auto snapshot = app_registry_get_all();
            for (const auto &app : snapshot) {
                ASSERT_NE(app.desc.id, nullptr);
                ASSERT_NE(app.desc.name, nullptr);
            }
        }
    });
    for (int i = 0; i < 32; ++i) {
        std::string id = "app-" + std::to_string(i);
        app_desc_t desc = desc_base();
        desc.id = id.c_str();
        ASSERT_EQ(app_registry_register(&desc), ESP_OK);
    }
    stop.store(true, std::memory_order_relaxed);
    reader.join();
    EXPECT_EQ(app_registry_get_count(), 33);
}

TEST_F(AppRegistryTest, SnapshotOwnsStringsAndExtensionsAfterUnregister)
{
    app_desc_t desc = desc_base();
    desc.file_extensions = kExtensoesTxt;
    desc.on_open_file = abrir_txt;
    ASSERT_EQ(app_registry_register(&desc), ESP_OK);
    auto snapshots = app_registry_get_all();
    ASSERT_EQ(snapshots.size(), 1u);
    ASSERT_EQ(app_registry_unregister("notas"), ESP_OK);
    EXPECT_STREQ(snapshots[0].desc.id, "notas");
    EXPECT_STREQ(snapshots[0].desc.name, "Notas");
    ASSERT_NE(snapshots[0].desc.file_extensions, nullptr);
    EXPECT_STREQ(snapshots[0].desc.file_extensions[0], "txt");
}

TEST_F(AppRegistryTest, CApiSnapshotOwnsAllBuffersAfterUnregister)
{
    app_desc_t desc = desc_base();
    desc.icon_bg_color = "#123456";
    desc.file_extensions = kExtensoesTxt;
    ASSERT_EQ(app_registry_register(&desc), ESP_OK);

    app_desc_t snapshot = {};
    ASSERT_EQ(app_registry_get_by_index(0, &snapshot), ESP_OK);
    ASSERT_EQ(app_registry_unregister("notas"), ESP_OK);
    EXPECT_STREQ(snapshot.id, "notas");
    EXPECT_STREQ(snapshot.name, "Notas");
    EXPECT_STREQ(snapshot.icon_symbol, "E");
    EXPECT_STREQ(snapshot.icon_bg_color, "#123456");
    EXPECT_STREQ(snapshot.file_extensions[0], "txt");
    app_registry_release_snapshot(&snapshot);
}

TEST_F(AppRegistryTest, CApiFindSnapshotOwnsBuffersAfterUnregister)
{
    app_desc_t desc = desc_base();
    desc.icon_bg_color = "#abcdef";
    desc.file_extensions = kExtensoesTxt;
    ASSERT_EQ(app_registry_register(&desc), ESP_OK);

    app_desc_t snapshot = {};
    ASSERT_EQ(app_registry_find_by_id("notas", &snapshot), ESP_OK);
    ASSERT_EQ(app_registry_unregister("notas"), ESP_OK);
    EXPECT_STREQ(snapshot.id, "notas");
    EXPECT_STREQ(snapshot.file_extensions[0], "txt");
    EXPECT_STREQ(snapshot.icon_bg_color, "#abcdef");
    app_registry_release_snapshot(&snapshot);
}

TEST_F(AppRegistryTest, SnapshotCopiesAndMovesKeepOwnedPointersBound)
{
    app_desc_t desc = desc_base();
    desc.file_extensions = kExtensoesTxt;
    ASSERT_EQ(app_registry_register(&desc), ESP_OK);

    auto original = app_registry_get_all();
    auto copied = original;
    auto moved = std::move(copied);
    app_desc_snapshot_t assigned;
    assigned = original[0];
    app_desc_snapshot_t move_assigned;
    move_assigned = std::move(assigned);
    EXPECT_STREQ(moved[0].id, "notas");
    EXPECT_STREQ(move_assigned.file_extensions[0], "txt");
}

} // namespace
