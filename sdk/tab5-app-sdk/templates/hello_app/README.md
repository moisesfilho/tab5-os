# Hello App - Tab5 OS

Template inicial para criacao de novas aplicacoes em WebAssembly para o Tab5 OS.

## Como Compilar e Empacotar

1. Certifique-se de que o compilador Clang com suporte a Wasm ou o `wasi-sdk` esta configurado.
2. Execute o comando de empacotamento:
   ```bash
   python3 ../../tools/pack.py .
   ```
3. O pacote resultante `com.tab5.hello.tab5pkg` sera gerado na pasta `dist/`.
4. Copie o pacote para o cartao SD em `/sdcard/apps/com.tab5.hello.tab5pkg` ou abra via app Gerenciador de Armazenamento / Arquivos.

## Manifesto (`manifest.json`)

O `pack.py` empacota o `manifest.json` junto com o `app.wasm`. Campos de ícone suportados:

| Campo | Exemplo | Descrição |
| :--- | :--- | :--- |
| `icon_symbol` / `icon.symbol` | `"LV_SYMBOL_EDIT"` ou `">_"` | Glifo do desktop. Aceita nomes de macro `LV_SYMBOL_*` (resolvidos automaticamente) ou texto/glifo cru. |
| `icon_bg_color` / `icon.bg_color` | `"#F59E0B"` | Cor de fundo da caixa do ícone (hex RGB). Opcional. |

```json
{
  "id": "com.tab5.hello",
  "name": "Hello Tab5",
  "entry": "app.wasm",
  "icon_symbol": ">_",
  "icon_bg_color": "#3B82F6",
  "permissions": ["ui_keyboard"]
}
```
