/**
 * @file tab5_wasm_runtime.cpp
 * @brief Implementação do Motor WAMR e Execução em Sandbox
 */

#include "tab5_wasm_runtime.h"
#include "tab5_host_abi.h"
#include "tab5_lifecycle_host.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <pthread.h>

#include "wasm_export.h"
static const char *TAG = "tab5_wasm";

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_pthread.h"
#define LOG_I(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...) ESP_LOGW(TAG, fmt, ##__VA_ARGS__)
#define LOG_E(fmt, ...) ESP_LOGE(TAG, fmt, ##__VA_ARGS__)
#define HAVE_WAMR 1
#else
#define LOG_E(fmt, ...) printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
#define LOG_W(fmt, ...) printf("[WARN] " fmt "\n", ##__VA_ARGS__)
#define LOG_I(fmt, ...) printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define HAVE_WAMR 0
#endif

static bool s_runtime_initialized = false;

tab5_err_t tab5_wasm_runtime_init(void)
{
    if (s_runtime_initialized) {
        return TAB5_OK;
    }

#if HAVE_WAMR
    RuntimeInitArgs init_args;
    memset(&init_args, 0, sizeof(RuntimeInitArgs));

    const uint32_t pool_size = 4 * 1024 * 1024;
#ifdef ESP_PLATFORM
    static uint8_t *s_wamr_pool_buf = (uint8_t *)heap_caps_malloc(pool_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint32_t real_pool_size = pool_size;
    if (s_wamr_pool_buf == nullptr) {
        real_pool_size = 512 * 1024;
        s_wamr_pool_buf = (uint8_t *)malloc(real_pool_size);
    }
#else
    static uint8_t *s_wamr_pool_buf = (uint8_t *)malloc(pool_size);
    uint32_t real_pool_size = pool_size;
#endif

    if (s_wamr_pool_buf != nullptr) {
        init_args.mem_alloc_type = Alloc_With_Pool;
        init_args.mem_alloc_option.pool.heap_buf = s_wamr_pool_buf;
        init_args.mem_alloc_option.pool.heap_size = real_pool_size;
    } else {
        init_args.mem_alloc_type = Alloc_With_System_Allocator;
    }

    uint32_t symbol_count = 0;
    const tab5_native_symbol_t *symbols = tab5_host_abi_get_symbols(&symbol_count);

    static std::vector<NativeSymbol> s_ram_symbols;
    s_ram_symbols.resize(symbol_count);
    memcpy(s_ram_symbols.data(), symbols, sizeof(NativeSymbol) * symbol_count);

    init_args.native_module_name = "env";
    init_args.native_symbols = s_ram_symbols.data();
    init_args.n_native_symbols = symbol_count;

    if (!wasm_runtime_full_init(&init_args)) {
        LOG_E("Falha ao inicializar WAMR full_init");
        return TAB5_ERR_FAIL;
    }

    LOG_I("WAMR runtime inicializado com sucesso (%u simbolos nativos registrados)", (unsigned)symbol_count);
#endif

    s_runtime_initialized = true;
    return TAB5_OK;
}

#if HAVE_WAMR
static tab5_err_t tab5_wasm_load_from_bytes_direct(const uint8_t *bytes, size_t size, uint32_t stack_size,
                                                   uint32_t heap_size, tab5_app_context_t *ctx,
                                                   tab5_wasm_app_instance_t *out_inst)
{
    if (bytes == nullptr || size == 0 || out_inst == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

    if (!s_runtime_initialized) {
        tab5_err_t err = tab5_wasm_runtime_init();
        if (err != TAB5_OK) {
            return err;
        }
    }

    memset(out_inst, 0, sizeof(*out_inst));
    if (ctx != nullptr) {
        strncpy(out_inst->app_id, ctx->app_id, sizeof(out_inst->app_id) - 1);
        out_inst->host_ctx = ctx;
    }

    char error_buf[128] = {0};

#ifdef ESP_PLATFORM
    uint8_t *wasm_buf = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (wasm_buf == nullptr) {
        wasm_buf = (uint8_t *)malloc(size);
    }
#else
    uint8_t *wasm_buf = (uint8_t *)malloc(size);
#endif

    if (wasm_buf == nullptr) {
        LOG_E("Memoria insuficiente para carregar bytecode Wasm (%zu bytes)", size);
        return TAB5_ERR_NO_MEM;
    }
    memcpy(wasm_buf, bytes, size);

    wasm_module_t module = wasm_runtime_load(wasm_buf, (uint32_t)size, error_buf, sizeof(error_buf));
    if (module == nullptr) {
        LOG_E("Erro ao fazer load do modulo Wasm: %s", error_buf);
        free(wasm_buf);
        return TAB5_ERR_FAIL;
    }

    uint32_t real_stack = (stack_size > 0) ? stack_size : TAB5_WASM_DEFAULT_STACK_SIZE;
    uint32_t real_heap = (heap_size > 0) ? heap_size : TAB5_WASM_DEFAULT_HEAP_SIZE;

    wasm_module_inst_t module_inst =
        wasm_runtime_instantiate(module, real_stack, real_heap, error_buf, sizeof(error_buf));
    if (module_inst == nullptr) {
        LOG_E("Erro ao instanciar modulo Wasm: %s", error_buf);
        wasm_runtime_unload(module);
        free(wasm_buf);
        return TAB5_ERR_FAIL;
    }

    wasm_exec_env_t exec_env = wasm_runtime_create_exec_env(module_inst, real_stack);
    if (exec_env == nullptr) {
        LOG_E("Erro ao criar exec_env Wasm");
        wasm_runtime_deinstantiate(module_inst);
        wasm_runtime_unload(module);
        free(wasm_buf);
        return TAB5_ERR_NO_MEM;
    }

    out_inst->module = (void *)module;
    out_inst->module_inst = (void *)module_inst;
    out_inst->exec_env = (void *)exec_env;
    out_inst->wasm_buf = wasm_buf;
    out_inst->wasm_buf_size = size;
    out_inst->is_running = true;

    LOG_I("App Wasm %s instanciada com sucesso (Stack=%u, Heap=%u)", out_inst->app_id[0] ? out_inst->app_id : "unnamed",
          (unsigned)real_stack, (unsigned)real_heap);
    return TAB5_OK;
}

struct WasmLoadInternalArgs {
    const uint8_t *bytes;
    size_t size;
    uint32_t stack_size;
    uint32_t heap_size;
    tab5_app_context_t *ctx;
    tab5_wasm_app_instance_t *out_inst;
    tab5_err_t result;
};

static void *wasm_load_pthread_worker(void *arg)
{
    auto *a = (WasmLoadInternalArgs *)arg;
    a->result = tab5_wasm_load_from_bytes_direct(a->bytes, a->size, a->stack_size, a->heap_size, a->ctx, a->out_inst);
    return nullptr;
}
#endif

tab5_err_t tab5_wasm_load_from_bytes(const uint8_t *bytes, size_t size, uint32_t stack_size, uint32_t heap_size,
                                     tab5_app_context_t *ctx, tab5_wasm_app_instance_t *out_inst)
{
#if HAVE_WAMR
#ifdef ESP_PLATFORM
    esp_pthread_cfg_t pcfg = esp_pthread_get_default_config();
    pcfg.stack_size = 64 * 1024;
    pcfg.stack_alloc_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    pcfg.inherit_cfg = false;
    esp_pthread_set_cfg(&pcfg);
#endif

    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64 * 1024);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    WasmLoadInternalArgs args = {
        .bytes = bytes,
        .size = size,
        .stack_size = stack_size,
        .heap_size = heap_size,
        .ctx = ctx,
        .out_inst = out_inst,
        .result = TAB5_ERR_FAIL,
    };

    int rc = pthread_create(&thread, &attr, wasm_load_pthread_worker, &args);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        LOG_E("pthread_create failed for wasm load: %d", rc);
        return TAB5_ERR_FAIL;
    }

    pthread_join(thread, nullptr);
    return args.result;
#else
    (void)stack_size;
    (void)heap_size;
    memset(out_inst, 0, sizeof(*out_inst));
    if (ctx != nullptr) {
        strncpy(out_inst->app_id, ctx->app_id, sizeof(out_inst->app_id) - 1);
        out_inst->host_ctx = ctx;
    }
    out_inst->module = (void *)(uintptr_t)0x1;
    out_inst->module_inst = (void *)(uintptr_t)0x2;
    out_inst->exec_env = (void *)(uintptr_t)0x3;
    out_inst->wasm_buf = (uint8_t *)malloc(size);
    if (out_inst->wasm_buf) {
        memcpy(out_inst->wasm_buf, bytes, size);
    }
    out_inst->wasm_buf_size = size;
    out_inst->is_running = true;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_wasm_load_from_file(const char *wasm_path, uint32_t stack_size, uint32_t heap_size,
                                    tab5_app_context_t *ctx, tab5_wasm_app_instance_t *out_inst)
{
    if (wasm_path == nullptr || out_inst == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

    FILE *f = fopen(wasm_path, "rb");
    if (f == nullptr) {
        LOG_E("Falha ao abrir arquivo Wasm: %s", wasm_path);
        return TAB5_ERR_NOT_FOUND;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 8 * 1024 * 1024) {
        fclose(f);
        LOG_E("Tamanho invalido do arquivo Wasm: %ld bytes", fsize);
        return TAB5_ERR_INVALID_ARG;
    }

    size_t size = (size_t)fsize;
    uint8_t *buf = (uint8_t *)malloc(size);
    if (buf == nullptr) {
        fclose(f);
        return TAB5_ERR_NO_MEM;
    }

    size_t read_bytes = fread(buf, 1, size, f);
    fclose(f);

    if (read_bytes != size) {
        free(buf);
        return TAB5_ERR_FAIL;
    }

    tab5_err_t err = tab5_wasm_load_from_bytes(buf, size, stack_size, heap_size, ctx, out_inst);
    free(buf);
    return err;
}

#if HAVE_WAMR
static tab5_err_t tab5_wasm_call_function_direct(tab5_wasm_app_instance_t *inst, const char *func_name, uint32_t argc,
                                                 uint32_t *argv)
{
    if (inst == nullptr || func_name == nullptr || !inst->is_running) {
        return TAB5_ERR_INVALID_ARG;
    }

    wasm_module_inst_t module_inst = (wasm_module_inst_t)inst->module_inst;
    wasm_exec_env_t exec_env = (wasm_exec_env_t)inst->exec_env;

    wasm_function_inst_t func = wasm_runtime_lookup_function(module_inst, func_name);
    if (func == nullptr) {
        LOG_W("Funcao %s nao encontrada no modulo Wasm", func_name);
        return TAB5_ERR_NOT_FOUND;
    }

    uint32_t local_argv[4] = {0};
    if (argv != nullptr && argc > 0) {
        for (uint32_t i = 0; i < argc && i < 4; i++) {
            local_argv[i] = argv[i];
        }
    }

    if (!wasm_runtime_call_wasm(exec_env, func, argc, local_argv)) {
        const char *exception = wasm_runtime_get_exception(module_inst);
        LOG_E("Excecao na execucao Wasm [%s]: %s", func_name, exception != nullptr ? exception : "desconhecida");
        return TAB5_ERR_FAIL;
    }

    if (argv != nullptr && argc > 0) {
        argv[0] = local_argv[0];
    }

    return TAB5_OK;
}

struct WasmCallInternalArgs {
    tab5_wasm_app_instance_t *inst;
    const char *func_name;
    uint32_t argc;
    uint32_t *argv;
    tab5_err_t result;
};

static void *wasm_call_pthread_worker(void *arg)
{
    auto *a = (WasmCallInternalArgs *)arg;
    a->result = tab5_wasm_call_function_direct(a->inst, a->func_name, a->argc, a->argv);
    return nullptr;
}
#endif

tab5_err_t tab5_wasm_call_function(tab5_wasm_app_instance_t *inst, const char *func_name, uint32_t argc, uint32_t *argv)
{
    if (inst == nullptr || func_name == nullptr || !inst->is_running) {
        return TAB5_ERR_INVALID_ARG;
    }

#if HAVE_WAMR
#ifdef ESP_PLATFORM
    esp_pthread_cfg_t pcfg = esp_pthread_get_default_config();
    pcfg.stack_size = 64 * 1024;
    pcfg.stack_alloc_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    pcfg.inherit_cfg = false;
    esp_pthread_set_cfg(&pcfg);
#endif

    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64 * 1024);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    WasmCallInternalArgs args = {
        .inst = inst,
        .func_name = func_name,
        .argc = argc,
        .argv = argv,
        .result = TAB5_ERR_FAIL,
    };

    int rc = pthread_create(&thread, &attr, wasm_call_pthread_worker, &args);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        LOG_E("pthread_create failed for wasm call: %d", rc);
        return TAB5_ERR_FAIL;
    }

    pthread_join(thread, nullptr);
    return args.result;
#else
    (void)argc;
    (void)argv;
    return TAB5_OK;
#endif
}

tab5_err_t tab5_wasm_unload(tab5_wasm_app_instance_t *inst)
{
    if (inst == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

#if HAVE_WAMR
    if (inst->exec_env != nullptr) {
        wasm_runtime_destroy_exec_env((wasm_exec_env_t)inst->exec_env);
        inst->exec_env = nullptr;
    }

    if (inst->module_inst != nullptr) {
        wasm_runtime_deinstantiate((wasm_module_inst_t)inst->module_inst);
        inst->module_inst = nullptr;
    }

    if (inst->module != nullptr) {
        wasm_runtime_unload((wasm_module_t)inst->module);
        inst->module = nullptr;
    }

    if (inst->wasm_buf != nullptr) {
        free(inst->wasm_buf);
        inst->wasm_buf = nullptr;
        inst->wasm_buf_size = 0;
    }
#else
    if (inst->wasm_buf != nullptr) {
        free(inst->wasm_buf);
        inst->wasm_buf = nullptr;
    }
#endif

    inst->is_running = false;
    LOG_I("App Wasm %s descarregada com sucesso", inst->app_id[0] ? inst->app_id : "unnamed");
    return TAB5_OK;
}

void tab5_wasm_runtime_destroy(void)
{
#if HAVE_WAMR
    if (s_runtime_initialized) {
        wasm_runtime_destroy();
        s_runtime_initialized = false;
        LOG_I("WAMR runtime finalizado");
    }
#else
    s_runtime_initialized = false;
#endif
}
