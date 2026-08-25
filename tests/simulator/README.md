# Simulador host do tab5_os

Regressão visual: roda a UI real (`os/shell` + apps `ui_*.cpp`) sobre o LVGL
vendido em `managed_components/lvgl__lvgl`, em janela SDL 720×1280, e compara
capturas contra imagens douradas (`goldens/`). Reproduz o fluxo do firmware
(splash, desktop, apps) sem precisar de hardware.

## Dependências

- SDL2 (`libsdl2-dev`)
- Python 3 + Pillow (só para o comparador de imagens)
- `gnutimeout` (pacote `gnu-coreutils`): o simulador não responde a SIGTERM em
  loop de render; o script usa `-k 2` para forçar SIGKILL.

## Compilação

```bash
cmake -S tests/simulator -B build-sim
cmake --build build-sim -j "$(nproc)"
```

O binário fica em `build-sim/tab5_sim`.

## Como rodar

```bash
# Roda todos os cenários e compara contra os goldens
tools/ci/run_sim_tests.sh

# Apenas um cenário
tools/ci/run_sim_tests.sh --scenario shell_desktop

# Modo interativo (janela aberta na área de trabalho)
./build-sim/tab5_sim --interactive
```

## Modos do binário

| Modo | Uso | Descrição |
|------|-----|-----------|
| `--interactive [DIR]` | janela SDL | teclas abaixo; `S` salva captura em DIR |
| `--scenario NOME [--out DIR] [--update-goldens]` | headless-ish | executa as ações do cenário e captura em `tests/simulator/out/<NOME>` (ou nos goldens com `--update-goldens`) |
| `--list` | lista cenários | imprime nome + descrição |

No modo cenário o relógio é congelado (`--wrap=time`/`localtime_r`,
TZ -3 → 21:00) e os backends respondem sempre igual, para capturas
determinísticas.

## Teclas do modo interativo

| Tecla | Ação |
|-------|------|
| `S` | salva captura atual |
| `1`–`9`, `0` | abre apps na ordem do registro |
| `M` | Música |
| `D` | desktop |
| `P` | menu de energia |
| `ESC` | sai |

## Atualizar goldens

Quando a UI mudar de propósito, regenere os dourados e confira visualmente
antes de commitar:

```bash
tools/ci/run_sim_tests.sh --update-goldens
```

Os BMPs ficam em `tests/simulator/goldens/<cenario>/01_*.bmp`. Os PNGs de
pré-visualização vão para `tests/simulator/preview/`.

## Critério do comparador

`tools/ci/compare_images.py` compara cada captura `out/` contra o golden:
- mesmo nome de arquivo e dimensões (720×1280);
- proporção de pixels divergentes (delta RGB por canal > 48) menor que 0,5%
  da área (tolerância para anti-aliasing/redesenho);
- em falha, grava PNG com as regiões diferentes em vermelho em
  `tests/simulator/diffs/` e relatório em `tests/simulator/report.txt`.

## Fixtures e redirecionamento de `/sdcard`

Os acessos a `/sdcard/...` dos módulos de produção são interceptados por
`-Wl,--wrap` (`fopen/open/mkdir/opendir/stat/time/localtime_r`) e
redirecionados para um tmpdir (`path_redirect`). Cada cenário planta seus
fixtures (ex.: `app_gallery` cria `/sdcard/imagens/paisagem.bmp`) antes de
abrir o app. A pasta de fotos da galeria é `/sdcard/imagens` (não `/fotos`).

## Estrutura

```
tests/simulator/
  CMakeLists.txt          build standalone (lvgl + SDL2 + wraps)
  lv_conf.h               conf do LVGL do sim (RGB565, SDL, snapshot, malloc clib)
  main.cpp                boot, modo interativo, runner de cenários
  scenarios/              definições dos 15 cenários + injeção de clique SDL
  shims/                  bsp/freertos/esp_* , sim_time, sim_capture, mocks
  goldens/<cenario>/      imagens de referência (não commitar atualizações sem revisão)
  out/                    capturas da última execução (gerado)
  diffs/                  PNGs de diferença em falha (gerado)
```
