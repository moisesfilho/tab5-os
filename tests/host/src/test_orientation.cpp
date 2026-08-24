#include "orientation.h"

#include <gtest/gtest.h>

namespace {

lv_disp_rotation_t feed(float ax, float ay)
{
    return orientation_update(ax, ay, 0.0F);
}

} // namespace

class OrientationTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        orientation_reset();
    }
};

TEST_F(OrientationTest, FlatMantemRotacaoAtual)
{
    EXPECT_EQ(feed(0.05F, 0.05F), LV_DISPLAY_ROTATION_0);
    orientation_set_current(LV_DISPLAY_ROTATION_180);
    EXPECT_EQ(feed(0.10F, -0.10F), LV_DISPLAY_ROTATION_180);
}

TEST_F(OrientationTest, GravidadePositivaYJaEmRotacaoZeroRetornaImediato)
{
    EXPECT_EQ(feed(0.0F, 1.0F), LV_DISPLAY_ROTATION_0);
}

TEST_F(OrientationTest, TransicaoExigeCincoLeiturasEstaveis)
{
    EXPECT_EQ(feed(0.0F, -1.0F), LV_DISPLAY_ROTATION_0);
    for (int i = 0; i < 4; ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(feed(0.0F, -1.0F), LV_DISPLAY_ROTATION_0);
    }
    EXPECT_EQ(feed(0.0F, -1.0F), LV_DISPLAY_ROTATION_180);
}

TEST_F(OrientationTest, DebounceInterrompidoRecomecaContagem)
{
    EXPECT_EQ(feed(0.0F, -1.0F), LV_DISPLAY_ROTATION_0);
    EXPECT_EQ(feed(0.0F, -1.0F), LV_DISPLAY_ROTATION_0);

    /* Alvos alternados reiniciam o contador de estabilidade */
    EXPECT_EQ(feed(1.0F, 0.0F), LV_DISPLAY_ROTATION_0);
    EXPECT_EQ(feed(-1.0F, 0.0F), LV_DISPLAY_ROTATION_0);

    /* Recomeca do zero: rearma o alvo e soma cinco leituras consecutivas */
    EXPECT_EQ(feed(0.0F, -1.0F), LV_DISPLAY_ROTATION_0);
    for (int i = 0; i < 4; ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(feed(0.0F, -1.0F), LV_DISPLAY_ROTATION_0);
    }
    EXPECT_EQ(feed(0.0F, -1.0F), LV_DISPLAY_ROTATION_180);
}

TEST_F(OrientationTest, GravidadePositivaXMapeiaRotacao90)
{
    EXPECT_EQ(feed(1.0F, 0.0F), LV_DISPLAY_ROTATION_0);
    for (int i = 0; i < 4; ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(feed(1.0F, 0.0F), LV_DISPLAY_ROTATION_0);
    }
    EXPECT_EQ(feed(1.0F, 0.0F), LV_DISPLAY_ROTATION_90);
}

TEST_F(OrientationTest, GravidadeNegativaXMapeiaRotacao270)
{
    EXPECT_EQ(feed(-1.0F, 0.0F), LV_DISPLAY_ROTATION_0);
    for (int i = 0; i < 4; ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(feed(-1.0F, 0.0F), LV_DISPLAY_ROTATION_0);
    }
    EXPECT_EQ(feed(-1.0F, 0.0F), LV_DISPLAY_ROTATION_270);
}

TEST_F(OrientationTest, SetCurrentDefineNovaLinhaDeBase)
{
    orientation_set_current(LV_DISPLAY_ROTATION_90);
    /* Mesma rotacao: retorno imediato sem debounce */
    EXPECT_EQ(feed(1.0F, 0.0F), LV_DISPLAY_ROTATION_90);
    /* Novo alvo: mantem a atual enquanto o debounce nao completa */
    EXPECT_EQ(feed(0.0F, 1.0F), LV_DISPLAY_ROTATION_90);
}
