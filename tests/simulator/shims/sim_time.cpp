#include "sim_time.hpp"

#include <ctime>

static bool s_frozen = false;

extern "C" {

time_t __real_time(time_t *t);
struct tm *__real_localtime_r(const time_t *timep, struct tm *result);

time_t __wrap_time(time_t *t)
{
    if (s_frozen) {
        if (t != nullptr) {
            *t = simtime::FROZEN_EPOCH;
        }
        return simtime::FROZEN_EPOCH;
    }
    return __real_time(t);
}

struct tm *__wrap_localtime_r(const time_t *timep, struct tm *result)
{
    if (s_frozen) {
        time_t fixed = simtime::FROZEN_EPOCH;
        return gmtime_r(&fixed, result);
    }
    return __real_localtime_r(timep, result);
}

} /* extern "C" */

namespace simtime {

void set_frozen(bool frozen)
{
    s_frozen = frozen;
}

} // namespace simtime
