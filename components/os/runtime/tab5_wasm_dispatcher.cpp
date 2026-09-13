#include "tab5_wasm_dispatcher.h"
#include "tab5_package_mgr.h"

#include <pthread.h>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <vector>

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
    tab5_wasm_dispatch_token_t *token;
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
/* Serializes the launch boundary with shutdown.  It is deliberately
 * separate from s_mutex: package_mgr_launch_direct may post other jobs and
 * must not be called while the queue mutex is held. */
static std::mutex s_launch_gate;
static std::condition_variable s_ready;
static std::deque<tab5_wasm_dispatch_job> s_jobs;
static pthread_t s_worker;
static bool s_started = false;
static bool s_stopping = false;
static size_t s_inflight = 0;
static bool s_worker_detached = false;
static thread_local bool s_in_worker = false;
static thread_local tab5_wasm_app_instance_t *s_current_instance = nullptr;

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0)
        return;
    std::strncpy(dst, src != nullptr ? src : "", dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static void execute_job(const tab5_wasm_dispatch_job &job)
{
    /* shutdown() changes this flag before joining the worker.  A job that was
     * still queued at that point must never reach the package manager (nor a
     * WASM instance).  Jobs already executing are accounted for by
     * s_inflight and are allowed to finish before shutdown() returns. */
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_stopping)
            return;
    }

    /* Revalidate after dequeue: unload may have advanced the generation while
     * this job was waiting.  A stale job must never enter a WASM wrapper. */
    if (job.token != nullptr && !tab5_wasm_dispatch_token_validate(job.token, job.generation))
        return;
    s_current_instance = job.instance;

    if (job.kind == JOB_LAUNCH) {
        std::lock_guard<std::mutex> launch_lock(s_launch_gate);
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (s_stopping)
                return;
        }
        (void)tab5_package_mgr_launch_direct(job.app_id, job.open_file_path[0] != '\0' ? job.open_file_path : nullptr);
        s_current_instance = nullptr;
        return;
    }

    tab5_wasm_app_instance_t *inst = job.instance;
    tab5_err_t err = TAB5_ERR_FAIL;
    if (job.kind == JOB_STRING) {
        err = tab5_wasm_call_string_function(inst, job.primary, job.value);
    } else {
        err = tab5_wasm_call_function(inst, job.primary, job.argc, const_cast<uint32_t *>(job.args));
    }
    if (err == TAB5_ERR_NOT_FOUND && job.alias[0] != '\0') {
        if (job.token != nullptr) {
            if (job.kind == JOB_STRING) {
                (void)tab5_wasm_call_string_function(inst, job.alias, job.value);
            } else {
                (void)tab5_wasm_call_function(inst, job.alias, job.argc, const_cast<uint32_t *>(job.args));
            }
        }
    }
    s_current_instance = nullptr;
}

static void *dispatcher_worker(void *)
{
    s_in_worker = true;
    for (;;) {
        tab5_wasm_dispatch_job job = {};
        {
            std::unique_lock<std::mutex> lock(s_mutex);
            s_ready.wait(lock, [] { return s_stopping || !s_jobs.empty(); });
            if (s_stopping) {
                for (const auto &queued : s_jobs)
                    tab5_wasm_dispatch_token_release(queued.token);
                s_jobs.clear();
                if (s_worker_detached) {
                    s_started = false;
                    s_worker_detached = false;
                    s_inflight = 0;
                    s_ready.notify_all();
                }
                return nullptr;
            }
            job = s_jobs.front();
            s_jobs.pop_front();
            ++s_inflight;
        }
        execute_job(job);
        const bool needs_unload =
            job.token != nullptr && job.instance != nullptr && tab5_wasm_instance_unload_pending(job.instance);
        tab5_wasm_dispatch_token_release(job.token);
        if (needs_unload)
            (void)tab5_wasm_unload(job.instance);
        tab5_package_mgr_process_pending_close();
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            --s_inflight;
        }
        s_ready.notify_all();
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
    /* Establish a linearization point with JOB_LAUNCH: either the launch has
     * acquired this gate and is allowed to finish before shutdown starts, or
     * shutdown wins and the worker rejects it without entering the package
     * manager. */
    if (s_in_worker) {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_started)
            return;
        s_stopping = true;
        for (const auto &job : s_jobs)
            tab5_wasm_dispatch_token_release(job.token);
        s_jobs.clear();
        s_worker_detached = true;
        pthread_detach(s_worker);
        s_ready.notify_all();
        return;
    }

    bool detached = false;
    {
        /* Serialize only the stop linearization point.  Do not retain the
         * launch gate while joining: a worker may already own the gate and
         * must be allowed to finish that launch before it observes stop. */
        std::lock_guard<std::mutex> launch_lock(s_launch_gate);
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_started)
            return;
        s_stopping = true;
        /* Reject/cancel every job that has not crossed the worker boundary.
         * In particular, a queued launch must not run after package/runtime
         * teardown has started. */
        for (const auto &job : s_jobs)
            tab5_wasm_dispatch_token_release(job.token);
        s_jobs.clear();
        detached = s_worker_detached;
    }
    s_ready.notify_all();
    if (detached) {
        std::unique_lock<std::mutex> lock(s_mutex);
        s_ready.wait(lock, [] { return !s_started; });
        return;
    }
    if (!pthread_equal(pthread_self(), s_worker))
        pthread_join(s_worker, nullptr);
    std::lock_guard<std::mutex> lock(s_mutex);
    for (const auto &job : s_jobs)
        tab5_wasm_dispatch_token_release(job.token);
    s_jobs.clear();
    s_inflight = 0;
    s_started = false;
}

extern "C" bool tab5_wasm_dispatcher_wait_idle(uint32_t timeout_ms)
{
    std::unique_lock<std::mutex> lock(s_mutex);
    if (!s_started) {
        return true;
    }
    return s_ready.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                            [] { return s_jobs.empty() && s_inflight == 0; });
}

extern "C" void tab5_wasm_dispatcher_cancel_instance(tab5_wasm_app_instance_t *inst)
{
    if (inst == nullptr)
        return;
    std::vector<tab5_wasm_dispatch_token_t *> released;
    std::lock_guard<std::mutex> lock(s_mutex);
    for (auto it = s_jobs.begin(); it != s_jobs.end();) {
        if (it->instance == inst) {
            released.push_back(it->token);
            it = s_jobs.erase(it);
        } else
            ++it;
    }
    for (auto *token : released)
        tab5_wasm_dispatch_token_release(token);
}

extern "C" bool tab5_wasm_dispatcher_is_current_instance(const tab5_wasm_app_instance_t *inst)
{
    return inst != nullptr && s_current_instance == inst;
}

static bool enqueue(const tab5_wasm_dispatch_job &job)
{
    if (tab5_wasm_dispatcher_init() != TAB5_OK)
        return false;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_stopping)
        return false;
    const size_t queue_size = s_jobs.size();
    constexpr size_t capacity = TAB5_WASM_DISPATCH_QUEUE_CAPACITY;
    if (queue_size >= capacity) {
        LOG_W("WASM dispatch queue full; dropping job");
        return false;
    }
    if (job.token != nullptr && !tab5_wasm_dispatch_token_validate(job.token, job.generation))
        return false;
    s_jobs.push_back(job);
    s_ready.notify_one();
    return true;
}

extern "C" bool tab5_wasm_dispatch_post_call(tab5_wasm_app_instance_t *inst, const char *primary, const char *alias,
                                             uint32_t argc, const uint32_t *argv)
{
    tab5_wasm_dispatch_job job = {};
    job.kind = JOB_CALL;
    if (inst == nullptr || (job.token = tab5_wasm_dispatch_token_acquire(inst, &job.generation)) == nullptr)
        return false;
    job.argc = argc > 4 ? 4 : argc;
    for (uint32_t i = 0; i < job.argc; ++i)
        job.args[i] = argv != nullptr ? argv[i] : 0;
    copy_text(job.primary, sizeof(job.primary), primary);
    copy_text(job.alias, sizeof(job.alias), alias);
    if (!enqueue(job)) {
        tab5_wasm_dispatch_token_release(job.token);
        return false;
    }
    return true;
}

extern "C" bool tab5_wasm_dispatch_post_string(tab5_wasm_app_instance_t *inst, const char *primary, const char *alias,
                                               const char *value)
{
    tab5_wasm_dispatch_job job = {};
    job.kind = JOB_STRING;
    if (inst == nullptr || (job.token = tab5_wasm_dispatch_token_acquire(inst, &job.generation)) == nullptr)
        return false;
    copy_text(job.primary, sizeof(job.primary), primary);
    copy_text(job.alias, sizeof(job.alias), alias);
    copy_text(job.value, sizeof(job.value), value);
    if (!enqueue(job)) {
        tab5_wasm_dispatch_token_release(job.token);
        return false;
    }
    return true;
}

extern "C" tab5_err_t tab5_wasm_dispatch_post_launch(const char *app_id, const char *open_file_path)
{
    tab5_wasm_dispatch_job job = {};
    job.kind = JOB_LAUNCH;
    copy_text(job.app_id, sizeof(job.app_id), app_id);
    copy_text(job.open_file_path, sizeof(job.open_file_path), open_file_path);
    return enqueue(job) ? TAB5_OK : TAB5_ERR_NO_MEM;
}
