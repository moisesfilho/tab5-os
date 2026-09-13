# Build dos aplicativos embutidos

## Regra obrigatoria

Use sempre o pipeline do sistema operacional:

```bash
bash tools/ci/build_embedded_apps.sh
idf.py build
idf.py -p /dev/ttyACM0 flash
```

O primeiro comando recompila os aplicativos com o wrapper de entrypoint usado
pelo runtime WAMR, valida a exportacao de `main` ou `app_main` e gera os
pacotes em `embedded_apps_pkg/`. O `idf.py build` atualiza `apps.bin`, que e a
particao SPIFFS incorporada ao firmware.

## Falha corrigida em setembro de 2026

Os scripts individuais `tab5-app-*/tools/build.sh` geravam WASM valido, mas
alguns artefatos nao exportavam `main` nem `app_main`. O pacote era encontrado
e instanciado, mas o runtime registrava `Nenhum entrypoint suportado` e
descarregava o aplicativo antes de criar a tela.

O pipeline oficial agora executa `tools/ci/validate_wasm_entrypoint.py` antes
do empacotamento. Assim, um aplicativo sem entrypoint suportado falha no build
e nao chega ao firmware.

## Verificacao manual

Depois do flash, o log UART deve conter, para cada app, as mensagens de
instanciacao e execucao. Para Notas, os indicadores esperados sao:

```text
App Wasm com.tab5.notas instanciada com sucesso
Bytecode Wasm carregado com sucesso para com.tab5.notas
```
