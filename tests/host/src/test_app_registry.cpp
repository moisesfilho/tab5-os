#include "app_registry.h"
#include "file_assoc.h"

#include <gtest/gtest.h>
#include <string>

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

    const app_desc_t *found = app_registry_find_by_id("notas");
    ASSERT_NE(found, nullptr);
    EXPECT_STREQ(found->name, "Notas");
    EXPECT_STREQ(found->icon_symbol, "E");

    found = app_registry_get_by_index(0);
    ASSERT_NE(found, nullptr);
    EXPECT_STREQ(found->id, "notas");

    EXPECT_EQ(app_registry_get_by_index(-1), nullptr);
    EXPECT_EQ(app_registry_get_by_index(1), nullptr);
    EXPECT_EQ(app_registry_find_by_id(nullptr), nullptr);
    EXPECT_EQ(app_registry_find_by_id("inexistente"), nullptr);
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

} // namespace
