# Etapa 2 — decisões visuais do ciclo 1

## Método e comparação pré-regeneração

O comparador foi mantido em `delta RGB 48` e tolerância de `0,5%`. Antes de
regenerar qualquer referência, os 17 cenários foram capturados em
`tests/simulator/out/` e comparados com os goldens de `HEAD`. A estatística
abaixo usa pixels cujo maior delta de canal é `>48`; as caixas são
`x1,y1–x2,y2` na imagem 720×1280. A coluna `out` é a captura pré-regeneração,
não o golden copiado depois.

| cenário/arquivo | golden anterior (SHA-256) | out pré-regeneração (SHA-256) | pixels / % | delta máx. | região |
|---|---|---|---:|---:|---|
| `app_bluetooth/01_bt.bmp` | `8bd7743326e3648ed5831e45b4e20e05f7bc582d77e9ceb0e02cf8cf9377aedf` | `e284887167353db3ad32ce5c41480a207e1f1b1115bf272e387259bc6f45564e` | 27581 / 2,993% | 231 | 14,70–680,516 |
| `app_calendar/01_calendar.bmp` | `d3cc16e5c6b1304087c2644205ee0a2bbc7518e18c060d0cfcd5c9b0bfdb1b13` | `e7bec94f2882b368b2ccc1a94d801c9af08be070de829ac82db2109dc1e67406` | 9377 / 1,017% | 207 | 13,70–706,522 |
| `app_chat/01_chat.bmp` | `a5488e5168c6c4db0ffe7b32b643a0f1c6c22b551ce5b9034ebeb0fa665d1aac` | `8f8b6c0de1363e0c9b0a6e67529c92de14ac30523de767974af1b4d59bab119b` | 14148 / 1,535% | 223 | 0,65–719,1274 |
| `app_files/01_files.bmp` | `4058791d903d3f5c393b344e542fb0d450158b5fa1adb6d0d37717546480c81a` | `437704eeee4fc7778be3bf44b4d74b0927bdf91523210102d63b00a4bd21a03c` | 10750 / 1,166% | 223 | 12,68–647,235 |
| `app_fileserver/01_fileserver.bmp` | `ea9ec6f32518f8347c8bb52451d2fcd4c7776ff69046d66f75e717355ce57b98` | `4e207d0529f72a1672cd41b43268109e84323d97c06a19a4d81ad7499ff41aa7` | 46425 / 5,037% | 231 | 14,70–704,434 |
| `app_music/01_music.bmp` | `c7ca30ff2c1779a681771287ce31610772dce71b6ecb860d111254f82f690e47` | `6b9909a6b3515ff94d5db74c45a6af6b5534378821186f3701899b3e8739c7be` | 22002 / 2,387% | 231 | 14,118–679,475 |
| `app_recorder/01_recorder.bmp` | `341cd46a108b8bdfd3a9dcabd4f356e92380cf7f33b8ed7c7ba0165dd8030d95` | `e496831071793b96f5ea3e8534cd440228ebb729d8e309ffa2706146e107ccb1` | 56072 / 6,084% | 231 | 14,70–690,506 |
| `app_wifi/01_wifi.bmp` | `6054e1a3d51efd1d8a32d86bbef45b036bf366d37775bf5edea2f505e1070b63` | `a293f4370e5e3f62531b74892bfc55e7a015d723ea520486556fc65473de086b` | 29061 / 3,153% | 231 | 12,70–680,629 |
| `shell_calendar_popup/01_calendar_popup.bmp` | `8879f1b9e64fdc90d5abeb715228d8bfa7cdac89593ea0de23983321eda5fd59` | `cacc3843b69a4e1984689ce8f955e5c2cd8232021517fe11ce8fe08a7c29260b` | 16762 / 1,819% | 255 | 42,396–665,518 |
| `shell_desktop/01_desktop.bmp` | `eba8e230cd8b661709992e603c418bbfc0c3bcc36d3b7e7997ee386bc2511fa6` | `6930f6f994c32d59ad69aceefe400fba4c9de93e29ac19270710f92da2e1b9eb` | 37822 / 4,104% | 255 | 42,236–665,518 |
| `shell_power/01_power_menu.bmp` | `b3f6ae26c15dafa62eae60dd2248d4b5d2c02ea727818ce5c4f498870458665a` | `4d8bcb662645adf821c56133d9bd22bf65b23ced205611e9b3a6409844351259` | 37822 / 4,104% | 255 | 42,236–665,518 |
| `shell_settings/01_settings.bmp` | `a619993e207550907593525e8c333f42dbf9fb3820ef1a3799faf7720de97109` | `a6539b0da8cb2eece147de4eeaae445787eecd1dd82c4d79ded7cd451b28f0b2` | 28916 / 3,138% | 255 | 390,236–665,514 |

## Decisão individual

Cada alteração abaixo foi revisada contra a implementação, o manifesto/SDK
do pacote e a captura visual. Nenhum golden foi alterado para esconder uma
falha do comparador.

1. **`app_bluetooth/01_bt.bmp`** — Região do conteúdo do app (14,70–680,516),
   delta máximo 231. A causa é a tela WASM Bluetooth reconstruída pelo runtime,
   em vez do host view nativo anterior. A mudança é intencional: o pacote e o
   manifesto agora são a fonte da UI. Revisado com o fluxo de lançamento e
   estado settled do cenário.
2. **`app_calendar/01_calendar.bmp`** — Conteúdo (13,70–706,522), máximo 207.
   O calendário passou a ser desenhado pelo pacote WASM/SDK. Golden atualizado
   intencionalmente após conferir título, grade e estado inicial.
3. **`app_chat/01_chat.bmp`** — Conteúdo e rodapé (0,65–719,1274), máximo 223.
   A causa é a composição atual do app Chat WASM, incluindo o layout declarado
   pelo pacote. Revisados boot, lançamento e ausência de interação pendente.
4. **`app_files/01_files.bmp`** — Lista (12,68–647,235), máximo 223. A tela
   agora reflete o Files WASM e seu sandbox de armazenamento. Revisados fixture,
   diretório inicial e dimensões da lista; alteração intencional.
5. **`app_fileserver/01_fileserver.bmp`** — Conteúdo (14,70–704,434), máximo
   231. A mudança funcional é a tela do pacote Fileserver e seus controles,
   não uma diferença de tolerância. Revisados estado inicial e contrato do
   servidor antes da atualização.
6. **`app_music/01_music.bmp`** — Painel (14,118–679,475), máximo 231. O app
   agora usa a UI WASM reconstruída e o estado de biblioteca previsto pelo
   pacote. Revisados fixture, lançamento e settle; golden intencional.
7. **`app_recorder/01_recorder.bmp`** — Conteúdo (14,70–690,506), máximo 231.
   A tela atual é a UI Recorder declarada pelo pacote WASM, não a tela nativa
   legada. Revisados controles, estado parado e captura determinística.
8. **`app_wifi/01_wifi.bmp`** — Conteúdo (12,70–680,629), máximo 231. A causa
   é a tela Wi-Fi do pacote e sua lista/estado inicial. Revisados backend simulado,
   lançamento e dados exibidos; mudança intencional.
9. **`shell_calendar_popup/01_calendar_popup.bmp`** — Popup
   (42,396–665,518), máximo 255. O registro final de apps/tile Sistema alterou
   a composição sob o popup. Revisados abertura, posicionamento e data congelada.
10. **`shell_desktop/01_desktop.bmp`** — Grade/tile (42,236–665,518), máximo
    255. O registro passou a conter os 12 pacotes e o tile Sistema. Revisados
    ordem, ícones e estado settled; golden representa a matriz pretendida.
11. **`shell_power/01_power_menu.bmp`** — Área de menu/desktop
    (42,236–665,518), máximo 255. O menu é sobreposto à nova matriz de desktop,
    portanto a diferença acompanha a alteração do registro. Revisados abertura,
    ações e fechamento sem alterar a tolerância.
12. **`shell_settings/01_settings.bmp`** — Grade (390,236–665,514), máximo
    255. Apenas a parte afetada pelo tile Sistema/registro mudou. Revisados
    tile, ordem e popup de configurações; golden intencional.

## `app_terminal` (não regenerado)

A comparação pré-regeneração foi registrada mesmo sem alteração do golden:
golden anterior `c3ac49e00bd90ce09c19d37d2c0bbc2f5b309d9113c67a13b450fc6cbe36dc23`,
captura `out` `8f39c05c41572d331c34fc8180f934719a18e7f5478510fa9c8bd7267cb434d4`,
4304 pixels (`0,467%`), delta máximo 207, 720×1280. A diferença está no
conteúdo do Terminal WASM (banner/prompt e ações do app) contra o golden vazio
legado; é efeito funcional da migração para o pacote, não ruído de renderização.
Como permanece abaixo de `0,5%` e o cenário passa, não foi regenerado nem a
diferença foi mascarada: o contrato visual continuará detectando uma expansão
maior dessa região.

Os cinco cenários restantes (`app_camera`, `app_gallery`, `app_notas`,
`app_teclado` e o próprio `app_terminal`) não tiveram goldens regenerados.
