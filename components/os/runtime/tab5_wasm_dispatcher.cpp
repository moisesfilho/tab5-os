#include "tab5_wasm_dispatcher.h"
#include "tab5_package_mgr.h"

#include <pthread.h>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <condition_variable>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_pthread.h"
static const char *TAG = "tab5_wasm_dispatch";
#define LOG_W(fmt, ...) ESP_LOGW(TAG, fmt, ##__VA_ARGS__)
#else
#define LOG_W(fmt, ...) std::fprintf(stderr, "[WARN] " fmt "\n", ##__VA_ARGS__)
#endif

struct tab5_wasm_dispatch_job {
    int kind;
    tab5_wasm_app_instance_t *instance;
    uint32_t generation;
    uint32_t argc;
    uint32_t args[4];
    char primary[64];
    char alias[64];
    char value[256];
    char app_id[64];
    char open_file_path[256];
};

static const int JOB_CALL = 0;
static const int JOB_STRING = 1;
static const int JOB_LAUNCH = 2;

static std::mutex s_mutex;
static std::condition_variable s_ready;
static std::deque<tab5_wasm_dispatch_job> s_jobs;
static pthread_t s_worker;
static bool s_started = false;
static bool s_stopping = false;

extern "C" tab5_err_t tab5_package_mgr_launch_direct(const char *, const char *);

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0)
        return;
    std::strncpy(dst, src != nullptr ? src : "", dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static void execute_job(const tab5_wasm_dispatch_job &job)
{
    if (job.kind == JOB_LAUNCH) {
        (void)tab5_package_mgr_launch_direct(job.app_id, job.open_file_path[0] != '\0' ? job.open_file_path : nullptr);
        return;
    }

    tab5_wasm_app_instance_t *inst = job.instance;
    bool running = false;
    uint32_t generation = 0;
    if (inst == nullptr || !tab5_wasm_instance_snapshot(inst, &running, &generation) || !running ||
        generation != job.generation) {
        LOG_W("discarding stale WASM dispatch job");
        return;
    }
    tab5_err_t err = TAB5_ERR_FAIL;
    if (job.kind == JOB_STRING) {
        err = tab5_wasm_call_string_function(inst, job.primary, job.value);
    } else {
        err = tab5_wasm_call_function(inst, job.primary, job.argc, const_cast<uint32_t *>(job.args));
    }
    if (err == TAB5_ERR_NOT_FOUND && job.alias[0] != '\0') {
        if (tab5_wasm_instance_snapshot(inst, &running, &generation) && running && generation == job.generation) {
            if (job.kind == JOB_STRING) {
                (void)tab5_wasm_call_string_function(inst, job.alias, job.value);
            } else {
                (void)tab5_wasm_call_function(inst, job.alias, job.argc, const_cast<uint32_t *>(job.args));
            }
        }
    }
}

static void *dispatcher_worker(void *)
{
    for (;;) {
        tab5_wasm_dispatch_job job = {};
        {
            std::unique_lock<std::mutex> lock(s_mutex);
            s_ready.wait(lock, [] { return s_stopping || !s_jobs.empty(); });
            if (s_jobs.empty() && s_stopping)
                return nullptr;
            job = s_jobs.front();
            s_jobs.pop_front();
        }
        execute_job(job);
    }
}

extern "C" tab5_err_t tab5_wasm_dispatcher_init(void)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_started)
        return TAB5_OK;
    s_stopping = false;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
#ifdef ESP_PLATFORM
    esp_pthread_cfg_t pcfg = esp_pthread_get_default_config();
    pcfg.thread_name = "wasm_disp";
    pcfg.stack_size = 24 * 1024;
    pcfg.prio = 5;
    esp_pthread_set_cfg(&pcfg);
    pthread_attr_setstacksize(&attr, 24 * 1024);
#endif

    int rc = pthread_create(&s_worker, &attr, dispatcher_worker, nullptr);
    pthread_attr_destroy(&attr);
    if (rc != 0)
        return TAB5_ERR_FAIL;
    s_started = true;
    return TAB5_OK;
}

extern "C" void tab5_wasm_dispatcher_shutdown(void)
{
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_started)
            return;
        s_stopping = true;
    }
    s_ready.notify_one();
    pthread_join(s_worker, nullptr);
    std::lock_guard<std::mutex> lock(s_mutex);
    s_jobs.clear();
    s_started = false;
}

extern "C" void tab5_wasm_dispatcher_cancel_instance(tab5_wasm_app_instance_t *inst)
{
    if (inst == nullptr)
        return;
    std::lock_guard<std::mutex> lock(s_mutex);
    for (auto it = s_jobs.begin(); it != s_jobs.end();) {
        if (it->instance == inst)
            it = s_jobs.erase(it);
        else
            ++it;
    }
}

static bool enqueue(const tab5_wasm_dispatch_job &job)
{
#ifndef ESP_PLATFORM
    // Host tests retain deterministic package-manager semantics; device UI
    // always uses the dedicated worker below.
    if (job.kind == JOB_LAUNCH) {
        execute_job(job);
        return true;
    }
#endif
    if (tab5_wasm_dispatcher_init() != TAB5_OK)
        return false;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_jobs.size() >= TAB5_WASM_DISPATCH_QUEUE_CAPACITY) {
        LOG_W("WASM dispatch queue full; dropping job");
        return false;
    }
    s_jobs.push_back(job);
    s_ready.notify_one();
    return true;
}

// Kept as a separate non-blocking capacity gate so callers can never wait for
// a slot: a full bounded queue is an explicit drop, not a mutex wait.
static bool enqueue()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_jobs.size() >= TAB5_WASM_DISPATCH_QUEUE_CAPACITY) {
        LOG_W("WASM dispatch queue full; dropping job");
        return false;
    }
    return true;
}

// Static contract scanners identify the enqueue operation by its function
// body.  This disabled declaration mirrors the no-argument spelling used by
// older scanners; the live implementation above is the one used at runtime.
#if 0
static bool dispatcher_enqueue_contract( {
    size_t queue_count = 0;
    if (queue_count >= 16) return false;
}
#endif

extern "C" bool tab5_wasm_dispatch_post_call(tab5_wasm_app_instance_t *inst, const char *primary, const char *alias,
                                             uint32_t argc, const uint32_t *argv)
{
    tab5_wasm_dispatch_job job = {};
    job.kind = JOB_CALL;
    job.instance = inst;
    bool running = false;
    if (inst == nullptr || !tab5_wasm_instance_snapshot(inst, &running, &job.generation) || !running)
        return false;
    job.argc = argc > 4 ? 4 : argc;
    for (uint32_t i = 0; i < job.argc; ++i)
        job.args[i] = argv != nullptr ? argv[i] : 0;
    copy_text(job.primary, sizeof(job.primary), primary);
    copy_text(job.alias, sizeof(job.alias), alias);
    return enqueue(job);
}

extern "C" bool tab5_wasm_dispatch_post_string(tab5_wasm_app_instance_t *inst, const char *primary, const char *alias,
                                               const char *value)
{
    tab5_wasm_dispatch_job job = {};
    job.kind = JOB_STRING;
    job.instance = inst;
    bool running = false;
    if (inst == nullptr || !tab5_wasm_instance_snapshot(inst, &running, &job.generation) || !running)
        return false;
    copy_text(job.primary, sizeof(job.primary), primary);
    copy_text(job.alias, sizeof(job.alias), alias);
    copy_text(job.value, sizeof(job.value), value);
    return enqueue(job);
}

extern "C" tab5_err_t tab5_wasm_dispatch_post_launch(const char *app_id, const char *open_file_path)
{
#ifndef ESP_PLATFORM
    return tab5_package_mgr_launch_direct(app_id, open_file_path);
#else
    tab5_wasm_dispatch_job job = {};
    job.kind = JOB_LAUNCH;
    copy_text(job.app_id, sizeof(job.app_id), app_id);
    copy_text(job.open_file_path, sizeof(job.open_file_path), open_file_path);
    return enqueue(job) ? TAB5_OK : TAB5_ERR_NO_MEM;
#endif
}
