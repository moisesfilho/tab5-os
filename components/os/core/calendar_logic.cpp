#include "calendar_logic.h"
#include "timezone_mgr.h"
#include <ctime>

namespace {

const char *const s_month_names[CALENDAR_MONTHS_COUNT] = {"Janeiro",  "Fevereiro", "Março",    "Abril",
                                                          "Maio",     "Junho",     "Julho",    "Agosto",
                                                          "Setembro", "Outubro",   "Novembro", "Dezembro"};

const char *const s_weekday_names[CALENDAR_DAYS_PER_WEEK] = {"Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sáb"};

const int s_days_in_month[CALENDAR_MONTHS_COUNT] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

} // namespace

bool calendar_logic_is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int calendar_logic_get_days_in_month(int year, int month)
{
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && calendar_logic_is_leap_year(year)) {
        return 29;
    }
    return s_days_in_month[month - 1];
}

int calendar_logic_get_first_day_of_week(int year, int month)
{
    if (month < 1 || month > 12) {
        return -1;
    }

    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int y = year;
    if (month < 3) {
        y -= 1;
    }

    int w = (y + y / 4 - y / 100 + y / 400 + t[month - 1] + 1) % 7;
    if (w < 0) {
        w += 7;
    }
    return w;
}

void calendar_logic_prev_month(int *year, int *month)
{
    if (year == nullptr || month == nullptr) {
        return;
    }
    *month -= 1;
    if (*month < 1) {
        *month = 12;
        *year -= 1;
    }
}

void calendar_logic_next_month(int *year, int *month)
{
    if (year == nullptr || month == nullptr) {
        return;
    }
    *month += 1;
    if (*month > 12) {
        *month = 1;
        *year += 1;
    }
}

const char *calendar_logic_get_month_name(int month)
{
    if (month < 1 || month > 12) {
        return "";
    }
    return s_month_names[month - 1];
}

const char *calendar_logic_get_weekday_name(int weekday)
{
    if (weekday < 0 || weekday >= CALENDAR_DAYS_PER_WEEK) {
        return "";
    }
    return s_weekday_names[weekday];
}

void calendar_logic_get_current_date(int *out_year, int *out_month, int *out_day)
{
    struct tm t_local;
    if (timezone_mgr_get_localtime(&t_local) != nullptr) {
        if (out_year != nullptr) {
            *out_year = t_local.tm_year + 1900;
        }
        if (out_month != nullptr) {
            *out_month = t_local.tm_mon + 1;
        }
        if (out_day != nullptr) {
            *out_day = t_local.tm_mday;
        }
    } else {
        if (out_year != nullptr) {
            *out_year = 2026;
        }
        if (out_month != nullptr) {
            *out_month = 1;
        }
        if (out_day != nullptr) {
            *out_day = 1;
        }
    }
}
