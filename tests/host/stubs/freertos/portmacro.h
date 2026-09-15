#pragma once

/* Porta host mínima: StackType_t (palavra da stack) e a macro de concorrência
 * que o fonte de produção pode exigir. O mock usa threads do C++11; não há
 * stack switching real. */

#include <stdint.h>

typedef uint32_t StackType_t;
