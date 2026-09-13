/**
 * @file tab5_wasm_runtime.cpp
 * @brief Implementação do Motor WAMR e Execução em Sandbox
 */

#include "tab5_wasm_runtime.h"
#include "tab5_host_abi.h"
#include "tab5_lifecycle_host.h"
#include "tab5_wasm_dispatcher.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <new>
#include <pthread.h>

static const char *TAG __attribute__((unused)) = "tab5_wasm";

/* Implementado pelo package manager; mantido como chokepoint pós-retorno para
 * que lifecycle/UI de uma app antiga não sejam destruídos no callback. */
extern "C" void tab5_package_mgr_process_pending_close(void);
extern "C" tab5_err_t tab5_package_mgr_close_active(void);

#if defined(ESP_PLATFORM) || defined(TAB5_SIM)
#include "wasm_export.h"
#endif

#if defined(ESP_PLATFORM)
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
#if defined(TAB5_SIM)
#define HAVE_WAMR 1
#else
#define HAVE_WAMR 0
#endif
#endif

static bool s_runtime_initialized = false;

/* The registry is the first ownership boundary.  A dispatcher never probes
 * state_mutex from a raw pointer: it first obtains a token from this table.
 * The token keeps dispatch_refs non-zero until the worker has released it. */
static std::mutex s_instance_registry_mutex;
static std::unordered_map<tab5_wasm_app_instance_t *, bool> s_instance_registry;

struct tab5_wasm_dispatch_token {
    tab5_wasm_app_instance_t *instance;
};

static void register_instance(tab5_wasm_app_instance_t *inst)
{
    std::lock_guard<std::mutex> lock(s_instance_registry_mutex);
    s_instance_registry[inst] = true;
}

static void unregister_instance(tab5_wasm_app_instance_t *inst)
{
    std::lock_guard<std::mutex> lock(s_instance_registry_mutex);
    s_instance_registry.erase(inst);
}

static pthread_mutex_t *instance_mutex(tab5_wasm_app_instance_t *inst)
{
    return inst != nullptr ? static_cast<pthread_mutex_t *>(inst->state_mutex) : nullptr;
}
static pthread_cond_t *instance_cond(tab5_wasm_app_instance_t *inst)
{
    return inst != nullptr ? static_cast<pthread_cond_t *>(inst->state_cond) : nullptr;
}
static void instance_lock(tab5_wasm_app_instance_t *inst)
{
    if (instance_mutex(inst) != nullptr)
        pthread_mutex_lock(instance_mutex(inst));
}
static void instance_unlock(tab5_wasm_app_instance_t *inst)
{
    if (instance_mutex(inst) != nullptr)
        pthread_mutex_unlock(instance_mutex(inst));
}
static bool init_instance_mutex(tab5_wasm_app_instance_t *inst)
{
    auto *mutex = static_cast<pthread_mutex_t *>(malloc(sizeof(pthread_mutex_t)));
    if (mutex == nullptr || pthread_mutex_init(mutex, nullptr) != 0) {
        free(mutex);
        return false;
    }
    inst->state_mutex = mutex;
    auto *cond = static_cast<pthread_cond_t *>(malloc(sizeof(pthread_cond_t)));
    if (cond == nullptr || pthread_cond_init(cond, nullptr) != 0) {
        free(cond);
        pthread_mutex_destroy(mutex);
        free(mutex);
        inst->state_mutex = nullptr;
        return false;
    }
    inst->state_cond = cond;
    return true;
}

extern "C" tab5_wasm_dispatch_token_t *tab5_wasm_dispatch_token_acquire(tab5_wasm_app_instance_t *inst,
                                                                        uint32_t *out_generation)
{
    if (inst == nullptr)
        return nullptr;
    std::lock_guard<std::mutex> registry_lock(s_instance_registry_mutex);
    if (s_instance_registry.count(inst) == 0) // NOLINT(readability-container-contains)
        return nullptr;
    instance_lock(inst);
    if (!inst->is_running || inst->unload_pending) {
        instance_unlock(inst);
        return nullptr;
    }
    auto *token = new (std::nothrow) tab5_wasm_dispatch_token_t{inst};
    if (token == nullptr) {
        instance_unlock(inst);
        return nullptr;
    }
    ++inst->dispatch_refs;
    if (out_generation != nullptr)
        *out_generation = inst->generation;
    instance_unlock(inst);
    return token;
}

extern "C" void tab5_wasm_dispatch_token_release(tab5_wasm_dispatch_token_t *token)
{
    if (token == nullptr)
        return;
    tab5_wasm_app_instance_t *inst = token->instance;
    instance_lock(inst);
    if (inst->dispatch_refs > 0)
        --inst->dispatch_refs;
    if (inst->dispatch_refs == 0 && inst->call_depth == 0 && instance_cond(inst) != nullptr)
        pthread_cond_broadcast(instance_cond(inst));
    instance_unlock(inst);
    delete token;
}

extern "C" bool tab5_wasm_dispatch_token_validate(const tab5_wasm_dispatch_token_t *token, uint32_t generation)
{
    if (token == nullptr || token->instance == nullptr)
        return false;
    auto *inst = token->instance;
    instance_lock(inst);
    const bool valid = inst->is_running && !inst->unload_pending && inst->generation == generation;
    instance_unlock(inst);
    return valid;
}
static void destroy_instance_mutex(tab5_wasm_app_instance_t *inst)
{
    auto *cond = instance_cond(inst);
    if (cond != nullptr) {
        pthread_cond_destroy(cond);
        free(cond);
        inst->state_cond = nullptr;
    }
    auto *mutex = instance_mutex(inst);
    if (mutex != nullptr) {
        pthread_mutex_destroy(mutex);
        free(mutex);
        inst->state_mutex = nullptr;
    }
}

static thread_local tab5_wasm_app_instance_t *s_admitted_instance = nullptr;
static thread_local uint32_t s_admitted_depth = 0;

static bool admit_call(tab5_wasm_app_instance_t *inst, uint32_t expected_generation, void **out_module_inst)
{
    if (inst == nullptr || instance_mutex(inst) == nullptr)
        return false;
    instance_lock(inst);
    const bool okay = inst->is_running && !inst->unload_pending &&
                      (expected_generation == 0 || inst->generation == expected_generation);
    if (okay) {
        ++inst->call_depth;
        ++inst->active_admissions;
        if (out_module_inst != nullptr)
            *out_module_inst = inst->module_inst;
        s_admitted_instance = inst;
        ++s_admitted_depth;
    }
    instance_unlock(inst);
    return okay;
}

static void release_call(tab5_wasm_app_instance_t *inst)
{
    if (inst == nullptr || instance_mutex(inst) == nullptr)
        return;
    instance_lock(inst);
    if (inst->call_depth > 0)
        --inst->call_depth;
    if (inst->active_admissions > 0)
        --inst->active_admissions;
    if (inst->call_depth == 0 && instance_cond(inst) != nullptr)
        pthread_cond_broadcast(instance_cond(inst));
    instance_unlock(inst);
    if (s_admitted_instance == inst && s_admitted_depth > 0) {
        if (--s_admitted_depth == 0)
            s_admitted_instance = nullptr;
    }
    /* Direct calls have no dispatcher token to run the post-job epilogue.
     * Complete a deferred unload when the final admission leaves. */
    if (tab5_wasm_instance_unload_pending(inst) && tab5_wasm_instance_call_depth(inst) == 0 &&
        !tab5_wasm_dispatcher_is_current_instance(inst)) {
        (void)tab5_wasm_unload(inst);
    }
}

extern "C" bool tab5_wasm_instance_snapshot(const tab5_wasm_app_instance_t *inst, bool *out_running,
                                            uint32_t *out_generation)
{
    auto *mutable_inst = const_cast<tab5_wasm_app_instance_t *>(inst);
    if (mutable_inst == nullptr || instance_mutex(mutable_inst) == nullptr)
        return false;
    instance_lock(mutable_inst);
    if (out_running)
        *out_running = mutable_inst->is_running && !mutable_inst->unload_pending;
    if (out_generation)
        *out_generation = mutable_inst->generation;
    instance_unlock(mutable_inst);
    return true;
}
extern "C" bool tab5_wasm_instance_is_running(const tab5_wasm_app_instance_t *inst)
{
    bool running = false;
    return tab5_wasm_instance_snapshot(inst, &running, nullptr) && running;
}
extern "C" uint32_t tab5_wasm_instance_call_depth(const tab5_wasm_app_instance_t *inst)
{
    auto *mutable_inst = const_cast<tab5_wasm_app_instance_t *>(inst);
    if (mutable_inst == nullptr || instance_mutex(mutable_inst) == nullptr)
        return 0;
    instance_lock(mutable_inst);
    uint32_t depth = mutable_inst->call_depth;
    instance_unlock(mutable_inst);
    return depth;
}
extern "C" bool tab5_wasm_instance_unload_pending(const tab5_wasm_app_instance_t *inst)
{
    auto *mutable_inst = const_cast<tab5_wasm_app_instance_t *>(inst);
    if (mutable_inst == nullptr || instance_mutex(mutable_inst) == nullptr)
        return false;
    instance_lock(mutable_inst);
    bool pending = mutable_inst->unload_pending;
    instance_unlock(mutable_inst);
    return pending;
}

tab5_err_t tab5_wasm_runtime_init(void)
{
    if (s_runtime_initialized) {
        return TAB5_OK;
    }

#if HAVE_WAMR
    RuntimeInitArgs init_args;
    memset(&init_args, 0, sizeof(RuntimeInitArgs));

    const uint32_t pool_size = 4 * 1024 * 1024;
#if defined(ESP_PLATFORM)
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

#if defined(ESP_PLATFORM)
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

    out_inst->module = (void *)module;
    out_inst->module_inst = (void *)module_inst;
    /* WAMR exec_env é thread-affine. É criado e destruído pelo worker de cada
     * chamada, nunca transportado entre workers. */
    out_inst->exec_env = nullptr;
    out_inst->wasm_buf = wasm_buf;
    out_inst->wasm_buf_size = size;
    out_inst->is_running = true;
    out_inst->generation = 1;
    if (!init_instance_mutex(out_inst)) {
        wasm_runtime_deinstantiate(module_inst);
        wasm_runtime_unload(module);
        free(wasm_buf);
        memset(out_inst, 0, sizeof(*out_inst));
        return TAB5_ERR_NO_MEM;
    }
    register_instance(out_inst);

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
    if (!wasm_runtime_init_thread_env()) {
        a->result = TAB5_ERR_FAIL;
        return nullptr;
    }
    a->result = tab5_wasm_load_from_bytes_direct(a->bytes, a->size, a->stack_size, a->heap_size, a->ctx, a->out_inst);
    wasm_runtime_destroy_thread_env();
    return nullptr;
}
#endif

tab5_err_t tab5_wasm_load_from_bytes(const uint8_t *bytes, size_t size, uint32_t stack_size, uint32_t heap_size,
                                     tab5_app_context_t *ctx, tab5_wasm_app_instance_t *out_inst)
{
    if (bytes == nullptr || size == 0 || out_inst == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

    // A instância nunca pode aparentar estar viva quando a criação do worker
    // falha antes de qualquer recurso do WAMR ser alocado.
    memset(out_inst, 0, sizeof(*out_inst));

#if HAVE_WAMR
#if defined(ESP_PLATFORM)
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
        memset(out_inst, 0, sizeof(*out_inst));
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
    out_inst->generation = 1;
    if (!init_instance_mutex(out_inst)) {
        free(out_inst->wasm_buf);
        memset(out_inst, 0, sizeof(*out_inst));
        return TAB5_ERR_NO_MEM;
    }
    register_instance(out_inst);
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

    if (fsize <= 0 || fsize > 8L * 1024L * 1024L) {
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
extern "C" tab5_err_t tab5_wasm_select_entrypoint(tab5_wasm_app_instance_t *inst, const char **out_func_name)
{
    if (inst == nullptr || out_func_name == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

    *out_func_name = nullptr;
    void *module_ptr = nullptr;
    if (!admit_call(inst, 0, &module_ptr))
        return TAB5_ERR_INVALID_STATE;
    wasm_module_inst_t module_inst = (wasm_module_inst_t)module_ptr;
    if (wasm_runtime_lookup_function(module_inst, "app_main") != nullptr) {
        *out_func_name = "app_main";
        release_call(inst);
        return TAB5_OK;
    }
    if (wasm_runtime_lookup_function(module_inst, "main") != nullptr) {
        *out_func_name = "main";
        release_call(inst);
        return TAB5_OK;
    }
    release_call(inst);
    return TAB5_ERR_NOT_FOUND;
}

static tab5_err_t tab5_wasm_call_function_direct(tab5_wasm_app_instance_t *inst, const char *func_name, uint32_t argc,
                                                 uint32_t *argv)
{
    if (inst == nullptr || func_name == nullptr) {
        LOG_W("Chamada Wasm invalida: inst=%p func=%p argc=%u", (void *)inst, (const void *)func_name, argc);
        return TAB5_ERR_INVALID_ARG;
    }

    void *module_ptr = nullptr;
    /* admit_call performs ++call_depth (inst->call_depth) while holding the
     * state mutex; release_call performs the matching decrement after
     * dispatch. */
    if (!admit_call(inst, 0, &module_ptr)) {
        LOG_W("Chamada Wasm rejeitada: instancia nao admitida para %s", func_name);
        return TAB5_ERR_INVALID_STATE;
    }
    wasm_module_inst_t module_inst = (wasm_module_inst_t)module_ptr;
    wasm_exec_env_t exec_env = wasm_runtime_create_exec_env(module_inst, TAB5_WASM_DEFAULT_STACK_SIZE);
    if (exec_env == nullptr) {
        release_call(inst);
        return TAB5_ERR_NO_MEM;
    }

    wasm_function_inst_t func = wasm_runtime_lookup_function(module_inst, func_name);
    if (func == nullptr) {
        LOG_W("Funcao %s nao encontrada no modulo Wasm", func_name);
        wasm_runtime_destroy_exec_env(exec_env);
        release_call(inst);
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
        wasm_runtime_destroy_exec_env(exec_env);
        release_call(inst);
        return TAB5_ERR_FAIL;
    }
    /* release_call performs --call_depth (inst->call_depth) only after WAMR returned. */

    if (argv != nullptr && argc > 0) {
        argv[0] = local_argv[0];
    }

    wasm_runtime_destroy_exec_env(exec_env);
    release_call(inst);
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
    if (!wasm_runtime_init_thread_env()) {
        a->result = TAB5_ERR_FAIL;
        return nullptr;
    }
    a->result = tab5_wasm_call_function_direct(a->inst, a->func_name, a->argc, a->argv);
    wasm_runtime_destroy_thread_env();
    return nullptr;
}
#endif

#if !HAVE_WAMR
extern "C" tab5_err_t tab5_wasm_select_entrypoint(tab5_wasm_app_instance_t *inst, const char **out_func_name)
{
    if (inst == nullptr || out_func_name == nullptr || !tab5_wasm_instance_is_running(inst)) {
        return TAB5_ERR_INVALID_ARG;
    }

    // O host de testes não incorpora WAMR. Mantemos o contrato de chamada
    // determinístico sem fingir que _start é um entrypoint de aplicação.
    *out_func_name = "app_main";
    return TAB5_OK;
}
#endif

// argv é mutado pelo dispatch WAMR (retorno de argumentos); permanece não-const
// mesmo no TU host, que não compila o backend WAMR.
tab5_err_t tab5_wasm_call_function(tab5_wasm_app_instance_t *inst, const char *func_name, uint32_t argc,
                                   uint32_t *argv) // NOLINT(readability-non-const-parameter)
{
    if (inst == nullptr || func_name == nullptr) {
        LOG_W("tab5_wasm_call_function recebeu argumento invalido: inst=%p func=%p argc=%u", (void *)inst,
              (const void *)func_name, argc);
        return TAB5_ERR_INVALID_ARG;
    }

#if HAVE_WAMR
    WasmCallInternalArgs call = {inst, func_name, argc, argv, TAB5_ERR_FAIL};
    pthread_t thread;
    int rc = pthread_create(&thread, nullptr, wasm_call_pthread_worker, &call);
    if (rc != 0) {
        return TAB5_ERR_FAIL;
    }
    pthread_join(thread, nullptr);
    tab5_err_t res = call.result;
    uint32_t call_depth = tab5_wasm_instance_call_depth(inst);
    bool unload_pending = tab5_wasm_instance_unload_pending(inst);
    if (call_depth == 0 && unload_pending) {
        (void)tab5_wasm_unload(inst);
    }
    tab5_package_mgr_process_pending_close();
    return res;
#else
    (void)argc;
    (void)argv;
    if (!admit_call(inst, 0, nullptr))
        return TAB5_ERR_INVALID_STATE;
    release_call(inst);
    tab5_package_mgr_process_pending_close();
    return TAB5_OK;
#endif
}

tab5_err_t tab5_wasm_call_string_function(tab5_wasm_app_instance_t *inst, const char *func_name, const char *value)
{
    if (inst == nullptr || func_name == nullptr || value == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

#if HAVE_WAMR
    void *module_ptr = nullptr;
    if (!admit_call(inst, 0, &module_ptr))
        return TAB5_ERR_INVALID_STATE;
    wasm_module_inst_t module_inst = (wasm_module_inst_t)module_ptr;
    size_t length = strlen(value) + 1;
    if (length > UINT32_MAX) {
        release_call(inst);
        return TAB5_ERR_INVALID_ARG;
    }

    uint32_t offset = wasm_runtime_module_malloc(module_inst, (uint32_t)length, nullptr);
    if (offset == 0) {
        release_call(inst);
        return TAB5_ERR_NO_MEM;
    }

    tab5_err_t result = TAB5_ERR_FAIL;
    char *module_string = (char *)wasm_runtime_addr_app_to_native(module_inst, offset);
    if (module_string != nullptr) {
        memcpy(module_string, value, length);
        uint32_t argv[1] = {offset};
        result = tab5_wasm_call_function(inst, func_name, 1, argv);
    }
    wasm_runtime_module_free(module_inst, offset);
    release_call(inst);
    /* Self-unload is completed only after this admission is released.  If the
     * current dispatch token is still held, tab5_wasm_unload intentionally
     * defers the final pass to the dispatcher post-token epilogue. */
    if (tab5_wasm_instance_unload_pending(inst))
        (void)tab5_wasm_unload(inst);
    tab5_package_mgr_process_pending_close();
    return result;
#else
    // O stub de host não instancia WAMR; mantém o contrato de chamada
    // determinístico sem tentar interpretar ponteiros como offsets Wasm.
    uint32_t argv[1] = {0};
    return tab5_wasm_call_function(inst, func_name, 1, argv);
#endif
}

tab5_err_t tab5_wasm_unload(tab5_wasm_app_instance_t *inst)
{
    if (inst == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

    if (instance_mutex(inst) == nullptr)
        return TAB5_OK; /* idempotent after final teardown */
    instance_lock(inst);
    if (!inst->is_running && inst->module == nullptr && inst->wasm_buf == nullptr) {
        instance_unlock(inst);
        return TAB5_OK;
    }
    /* Stop admission first. A callback may request its own unload; it must be
     * deferred to the call epilogue, while an external caller waits for every
     * admitted caller before touching WAMR pointers. */
    inst->is_running = false;
    inst->unload_pending = true;
    ++inst->generation;
    uint32_t call_depth = inst->call_depth;
    /* Keep compatibility with the lifecycle callback contract: a callback
     * that is represented only by call_depth (legacy/manual host fixtures)
     * has no admission token for this thread, so teardown is deferred. Real
     * calls always increment active_admissions and are waited below. */
    if (call_depth > 0) {
        if (inst->active_admissions == 0) {
            instance_unlock(inst);
            return TAB5_OK;
        }
    }
    instance_unlock(inst);

    /* Remove queued tokens after stopping admission.  The worker that already
     * crossed the boundary is covered by call_depth and dispatch_refs. */
    tab5_wasm_dispatcher_cancel_instance(inst);
    instance_lock(inst);
    if (s_admitted_instance == inst || tab5_wasm_dispatcher_is_current_instance(inst)) {
        instance_unlock(inst);
        return TAB5_OK;
    }
    while (inst->call_depth != 0 || inst->dispatch_refs != 0)
        pthread_cond_wait(instance_cond(inst), instance_mutex(inst));
    instance_unlock(inst);

#if HAVE_WAMR
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

    instance_lock(inst);
    inst->unload_pending = false;
    instance_unlock(inst);
    unregister_instance(inst);
    destroy_instance_mutex(inst);
    LOG_I("App Wasm %s descarregada com sucesso", inst->app_id[0] ? inst->app_id : "unnamed");
    return TAB5_OK;
}

void tab5_wasm_runtime_destroy(void)
{
    /* Stop and join the dispatcher first.  Otherwise a queued JOB_LAUNCH can
     * enter package_mgr_launch_direct while close_active() is tearing down the
     * active runtime. */
    tab5_wasm_dispatcher_shutdown();
    (void)tab5_package_mgr_close_active();
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
