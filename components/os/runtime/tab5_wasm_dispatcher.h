#pragma once

#include "tab5_wasm_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TAB5_WASM_DISPATCH_QUEUE_CAPACITY 16

tab5_err_t tab5_wasm_dispatcher_init(void);
void tab5_wasm_dispatcher_shutdown(void);
void tab5_wasm_dispatcher_cancel_instance(tab5_wasm_app_instance_t *inst);
bool tab5_wasm_dispatch_post_call(tab5_wasm_app_instance_t *inst, const char *primary, const char *alias, uint32_t argc,
                                  const uint32_t *argv);
bool tab5_wasm_dispatch_post_string(tab5_wasm_app_instance_t *inst, const char *primary, const char *alias,
                                    const char *value);
tab5_err_t tab5_wasm_dispatch_post_launch(const char *app_id, const char *open_file_path);

#ifdef __cplusplus
}
#endif
