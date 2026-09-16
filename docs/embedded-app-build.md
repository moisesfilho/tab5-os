# Build dos aplicativos embutidos

## Regra obrigatoria

Cada aplicativo pode ser recompilado e empacotado individualmente:

```bash
bash ../tab5-app-wifi/tools/build.sh
```

Os doze `tab5-app-*/tools/build.sh` delegam ao helper comum
`tab5-os/sdk/tab5-app-sdk/tools/build_wasm_app.sh`. O helper exige o SDK WASI,
`manifest.json` e `src/main.c`, limpa o `dist/`, compila, valida o entrypoint e
gera o `.tab5pkg`; portanto, não há fallback para WASM dummy.

Para reconstruir o bundle do sistema, use:

```bash
bash tools/ci/build_embedded_apps.sh [--app all|short-name|repo-dir]
idf.py build
idf.py -p /dev/ttyACM0 flash
```

O comando central limpa `embedded_apps_pkg/`, chama os scripts individuais e
copia seus pacotes para o bundle. Sem `--app`, processa os 12 apps; com um
short-name (por exemplo `--app notas`) ou caminho de repositório, processa
somente o selecionado e falha se ele não existir. O `idf.py build` atualiza `apps.bin`, que e a
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
