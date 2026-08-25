#pragma once

#include <cstdint>

/* Captura sincrona da tela ativa + layer_top em BMP 24-bit (mesmo formato
 * gerado pelo screenshot.cpp do firmware). Nao dispara flash/toast. */
bool sim_capture_to_bmp(const char *path);
