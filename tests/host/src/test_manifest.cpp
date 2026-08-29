#include <gtest/gtest.h>
#include "tab5_manifest.h"
#include <cstring>

TEST(ManifestTest, ParseValidJson)
{
    const char *json = R"({
        "id": "com.tab5.notas",
        "name": "Notas",
        "version": "1.2.0",
        "author": "Moisés Filho",
        "description": "Editor de texto desacoplado",
        "entry": "app.wasm",
        "icon": {
            "symbol": "LV_SYMBOL_EDIT",
            "bg_color": "#2196F3"
        },
        "file_associations": [".txt", ".md", ".log"],
        "permissions": [
            "storage.readwrite",
            "ui.keyboard"
        ],
        "stack_size": 32768,
        "heap_size": 262144
    })";

    tab5_manifest_t manifest = {};
    EXPECT_EQ(tab5_manifest_parse_json(json, &manifest), TAB5_OK);

    EXPECT_STREQ(manifest.id, "com.tab5.notas");
    EXPECT_STREQ(manifest.name, "Notas");
    EXPECT_STREQ(manifest.version, "1.2.0");
    EXPECT_STREQ(manifest.author, "Moisés Filho");
    EXPECT_STREQ(manifest.entry, "app.wasm");
    EXPECT_STREQ(manifest.icon_symbol, "LV_SYMBOL_EDIT");
    EXPECT_STREQ(manifest.icon_bg_color, "#2196F3");
    EXPECT_EQ(manifest.file_assoc_count, 3);
    EXPECT_STREQ(manifest.file_associations[0], ".txt");
    EXPECT_STREQ(manifest.file_associations[1], ".md");
    EXPECT_STREQ(manifest.file_associations[2], ".log");

    EXPECT_TRUE(manifest.permissions & TAB5_PERM_STORAGE_READ);
    EXPECT_TRUE(manifest.permissions & TAB5_PERM_STORAGE_WRITE);
    EXPECT_TRUE(manifest.permissions & TAB5_PERM_UI_KEYBOARD);
    EXPECT_FALSE(manifest.permissions & TAB5_PERM_NETWORK);

    EXPECT_EQ(manifest.stack_size, 32768u);
    EXPECT_EQ(manifest.heap_size, 262144u);
}

TEST(ManifestTest, ParseMinimalJsonDefaults)
{
    const char *json = R"({
        "id": "com.tab5.calc",
        "name": "Calculadora"
    })";

    tab5_manifest_t manifest = {};
    EXPECT_EQ(tab5_manifest_parse_json(json, &manifest), TAB5_OK);

    EXPECT_STREQ(manifest.id, "com.tab5.calc");
    EXPECT_STREQ(manifest.name, "Calculadora");
    EXPECT_STREQ(manifest.version, "1.0.0");
    EXPECT_STREQ(manifest.entry, "app.wasm");
    EXPECT_EQ(manifest.file_assoc_count, 0);
    EXPECT_EQ(manifest.permissions, 0u);
    EXPECT_EQ(manifest.stack_size, (uint32_t)TAB5_WASM_DEFAULT_STACK_SIZE);
}

TEST(ManifestTest, InvalidJsonAndMissingFields)
{
    tab5_manifest_t manifest = {};
    // Null arguments
    EXPECT_EQ(tab5_manifest_parse_json(nullptr, &manifest), TAB5_ERR_INVALID_ARG);
    EXPECT_EQ(tab5_manifest_parse_json("{}", nullptr), TAB5_ERR_INVALID_ARG);

    // Invalid JSON syntax
    EXPECT_EQ(tab5_manifest_parse_json("not a json", &manifest), TAB5_ERR_FAIL);

    // Missing required ID or Name
    EXPECT_EQ(tab5_manifest_parse_json("{\"name\": \"Sem ID\"}", &manifest), TAB5_ERR_INVALID_ARG);
    EXPECT_EQ(tab5_manifest_parse_json("{\"id\": \"com.tab5.noname\"}", &manifest), TAB5_ERR_INVALID_ARG);

    // Invalid characters in ID
    EXPECT_EQ(tab5_manifest_parse_json("{\"id\": \"com/tab5/bad\", \"name\": \"Bad\"}", &manifest),
              TAB5_ERR_INVALID_ARG);
}
