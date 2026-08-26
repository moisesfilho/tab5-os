#include "calendar_logic.h"
#include <gtest/gtest.h>

namespace {

TEST(CalendarLogicTest, LeapYearComputation)
{
    EXPECT_TRUE(calendar_logic_is_leap_year(2000));
    EXPECT_TRUE(calendar_logic_is_leap_year(2024));
    EXPECT_TRUE(calendar_logic_is_leap_year(2028));
    EXPECT_TRUE(calendar_logic_is_leap_year(2400));

    EXPECT_FALSE(calendar_logic_is_leap_year(1900));
    EXPECT_FALSE(calendar_logic_is_leap_year(2023));
    EXPECT_FALSE(calendar_logic_is_leap_year(2025));
    EXPECT_FALSE(calendar_logic_is_leap_year(2026));
    EXPECT_FALSE(calendar_logic_is_leap_year(2100));
}

TEST(CalendarLogicTest, DaysInMonthNormalAndLeap)
{
    /* 2026 (ano normal) */
    EXPECT_EQ(calendar_logic_get_days_in_month(2026, 1), 31);
    EXPECT_EQ(calendar_logic_get_days_in_month(2026, 2), 28);
    EXPECT_EQ(calendar_logic_get_days_in_month(2026, 3), 31);
    EXPECT_EQ(calendar_logic_get_days_in_month(2026, 4), 30);
    EXPECT_EQ(calendar_logic_get_days_in_month(2026, 5), 31);
    EXPECT_EQ(calendar_logic_get_days_in_month(2026, 6), 30);
    EXPECT_EQ(calendar_logic_get_days_in_month(2026, 7), 31);
    EXPECT_EQ(calendar_logic_get_days_in_month(2026, 8), 31);
    EXPECT_EQ(calendar_logic_get_days_in_month(2026, 9), 30);
    EXPECT_EQ(calendar_logic_get_days_in_month(2026, 10), 31);
    EXPECT_EQ(calendar_logic_get_days_in_month(2026, 11), 30);
    EXPECT_EQ(calendar_logic_get_days_in_month(2026, 12), 31);

    /* 2024 (ano bissexto) */
    EXPECT_EQ(calendar_logic_get_days_in_month(2024, 2), 29);

    /* Mês fora da faixa */
    EXPECT_EQ(calendar_logic_get_days_in_month(2026, 0), 0);
    EXPECT_EQ(calendar_logic_get_days_in_month(2026, 13), 0);
    EXPECT_EQ(calendar_logic_get_days_in_month(2026, -5), 0);
}

TEST(CalendarLogicTest, FirstDayOfWeekKnownDates)
{
    /* 0 = Domingo, 1 = Segunda, 2 = Terça, 3 = Quarta, 4 = Quinta, 5 = Sexta, 6 = Sábado */
    EXPECT_EQ(calendar_logic_get_first_day_of_week(2026, 8), 6); /* 1 de Agosto de 2026 é Sábado */
    EXPECT_EQ(calendar_logic_get_first_day_of_week(2026, 1), 4); /* 1 de Janeiro de 2026 é Quinta */
    EXPECT_EQ(calendar_logic_get_first_day_of_week(2025, 1), 3); /* 1 de Janeiro de 2025 é Quarta */
    EXPECT_EQ(calendar_logic_get_first_day_of_week(2024, 2), 4); /* 1 de Fevereiro de 2024 é Quinta */
    EXPECT_EQ(calendar_logic_get_first_day_of_week(2000, 1), 6); /* 1 de Janeiro de 2000 é Sábado */

    /* Mês inválido */
    EXPECT_EQ(calendar_logic_get_first_day_of_week(2026, 0), -1);
    EXPECT_EQ(calendar_logic_get_first_day_of_week(2026, 13), -1);
}

TEST(CalendarLogicTest, MonthNavigation)
{
    int y = 2026, m = 8;
    calendar_logic_prev_month(&y, &m);
    EXPECT_EQ(y, 2026);
    EXPECT_EQ(m, 7);

    calendar_logic_next_month(&y, &m);
    EXPECT_EQ(y, 2026);
    EXPECT_EQ(m, 8);

    /* Virada de ano para trás (Janeiro -> Dezembro) */
    y = 2026;
    m = 1;
    calendar_logic_prev_month(&y, &m);
    EXPECT_EQ(y, 2025);
    EXPECT_EQ(m, 12);

    /* Virada de ano para frente (Dezembro -> Janeiro) */
    y = 2025;
    m = 12;
    calendar_logic_next_month(&y, &m);
    EXPECT_EQ(y, 2026);
    EXPECT_EQ(m, 1);

    /* Segurança com ponteiros nulos */
    calendar_logic_prev_month(nullptr, &m);
    calendar_logic_next_month(&y, nullptr);
}

TEST(CalendarLogicTest, MonthAndWeekdayNames)
{
    EXPECT_STREQ(calendar_logic_get_month_name(1), "Janeiro");
    EXPECT_STREQ(calendar_logic_get_month_name(8), "Agosto");
    EXPECT_STREQ(calendar_logic_get_month_name(12), "Dezembro");
    EXPECT_STREQ(calendar_logic_get_month_name(0), "");
    EXPECT_STREQ(calendar_logic_get_month_name(13), "");

    EXPECT_STREQ(calendar_logic_get_weekday_name(0), "Dom");
    EXPECT_STREQ(calendar_logic_get_weekday_name(3), "Qua");
    EXPECT_STREQ(calendar_logic_get_weekday_name(6), "Sáb");
    EXPECT_STREQ(calendar_logic_get_weekday_name(-1), "");
    EXPECT_STREQ(calendar_logic_get_weekday_name(7), "");
}

TEST(CalendarLogicTest, CurrentDateRetrieval)
{
    int y = 0, m = 0, d = 0;
    calendar_logic_get_current_date(&y, &m, &d);
    EXPECT_GE(y, 2020);
    EXPECT_GE(m, 1);
    EXPECT_LE(m, 12);
    EXPECT_GE(d, 1);
    EXPECT_LE(d, 31);

    /* Chamada segura com parâmetros nulos */
    calendar_logic_get_current_date(nullptr, nullptr, nullptr);
}

} // namespace
