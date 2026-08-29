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
