# ABI WASM do Tab5 OS

## Exportacao do entrypoint

`TAB5_APP_EXPORT` fornece somente visibilidade publica em compilacoes WASM
(`__wasm__`, `__wasm32__` ou `__EMSCRIPTEN__`). Ele deve ser usado nos
callbacks que o host procura, como `tab5_app_on_ui_event` e
`tab5_app_on_theme_changed`; nao transforma callbacks em `app_main`.

O entrypoint ABI unico e declarado com `TAB5_APP_ENTRYPOINT_EXPORT`. O build de
apps gera um wrapper temporario que inclui o SDK, declara `main(int, char **)` e
define `tab5_wasm_app_main(void)`, chamando `main(0, NULL)`. O atributo
`export_name("app_main")` do macro exporta esse wrapper como `app_main`, sem
depender de `_start` e sem criar um segundo entrypoint. Em builds nativos, os
dois macros permanecem vazios.

## Ponteiros nos wrappers WAMR

As funcoes nativas registradas pelo runtime usam assinaturas WAMR. Os
parametros marcados com `*` sao ponteiros e os parametros marcados com `$` sao
strings. O WAMR valida esses argumentos e converte automaticamente os offsets
da memoria WASM para ponteiros nativos antes de invocar o wrapper.

Consequentemente, um wrapper nao deve chamar
`wasm_runtime_addr_app_to_native()` novamente sobre argumentos recebidos como
ponteiros. Essa conversao duplicada transforma um endereco nativo em um
offset invalido e pode fazer a API rejeitar buffers validos ou causar uma
excecao `out of bounds memory access`.

Exemplo da assinatura de `tab5_storage_scandir`:

```text
($*i*)i
```

Nesse caso, `rel_or_abs_path`, `entries` e `out_count` ja sao ponteiros
nativos quando chegam a `wasm_tab5_storage_scandir`. O wrapper deve apenas
validar argumentos conforme a politica da API e encaminha-los para
`tab5_storage_scandir`.

Use `wasm_runtime_addr_app_to_native()` somente quando o codigo estiver
manipulando explicitamente um offset WASM obtido de uma estrutura ou memoria
bruta, e nao em argumentos que o WAMR ja processou conforme a assinatura.

## Padroes de UI

### Eventos de widgets compostos

Widgets clicaveis que contem labels ou outros objetos podem entregar o evento
com o filho como alvo. O host deve procurar o handle no alvo e nos seus
ancestrais ate encontrar o objeto registrado. Aplicativos devem registrar o
callback no objeto clicavel externo e manter os filhos nao clicaveis quando a
linha inteira representa uma unica acao.

### Rolagem

Por padrão, a tela raiz, contêineres, toasts, labels, botões, switches e
sliders são criados sem `LV_OBJ_FLAG_SCROLLABLE` e com a scrollbar desligada.
Listas e textareas preservam a rolagem nativa. Para tornar um objeto genérico
rolável, use `tab5_ui_obj_set_scrollable(obj, true)`; para voltar ao estado
fixo, use `tab5_ui_obj_set_scrollable(obj, false)`. Essas chamadas também
configuram a scrollbar como `AUTO` e `OFF`, respectivamente.

Uma tela de aplicativo deve permanecer fixa quando a rolagem pertence a uma
lista. Configure a tela com `tab5_ui_obj_set_scrollable(screen, false)` e o
contêiner da lista com `tab5_ui_obj_set_scrollable(list, true)`. Reaplique o
fluxo e a configuração de rolagem depois de reconstruir os filhos, e use um
contêiner com tamanho definido para que a scrollbar seja calculada no elemento
correto.

Ao trocar o diretorio ou reconstruir uma lista, reposicione o contêiner no topo
antes de exibir os novos filhos. Isso evita que uma posicao de rolagem antiga
deixe uma lista curta aparentemente vazia.

### Limpeza durante callbacks

Nao destrua sincronicamente o objeto que recebeu o clique enquanto o callback
do evento ainda esta em execucao. Use `tab5_ui_obj_clean_deferred()` para
ocultar e remover os filhos no proximo ciclo do LVGL, permitindo que o callback
termine com o alvo valido.

### Handles e memoria

Cada objeto criado por `tab5_ui_host_register_obj()` ocupa uma entrada na
tabela de handles. Destruir um objeto LVGL sem liberar sua entrada causa
exaustao progressiva da tabela, mesmo que o heap ainda tenha memoria. Ao
limpar um contêiner, libere os handles de todos os descendentes, mas preserve
o handle do proprio contêiner se ele continuara sendo reutilizado.
