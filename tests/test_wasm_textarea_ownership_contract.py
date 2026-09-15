"""Contratos de ownership/segurança do ABI WASM: copy_text sem crescimento de heap.

O host anteriormente exportava `wasm_tab5_ui_textarea_get_text` que alocava
memória no heap do módulo via `wasm_runtime_module_malloc` para cada leitura
do textarea — sem mecanismo de liberação, o heap crescia sem limite até OOM
(travamento do Terminal após `help` + Enter).

A correção substituiu `get_text` por `copy_text`, que copia para um buffer
FORNECIDO pelo app WASM. O wrapper valida o endereço app via
`wasm_runtime_validate_app_addr`, traduz com `wasm_runtime_addr_app_to_native`
e chama `tab5_ui_textarea_copy_text(ta, native_buffer, capacity)`. Nenhuma
alocação de heap do módulo é feita.

Contratos (level: source — a execução dinâmica do WAMR exige o firmware):

1. O wrapper `wasm_tab5_ui_textarea_copy_text` NÃO contém
   `wasm_runtime_module_malloc` — o buffer é do app e é traduzido endereços.
2. O wrapper valida o endereço do buffer do app com
   `wasm_runtime_validate_app_addr` antes de converter com
   `wasm_runtime_addr_app_to_native`.
3. O wrapper retorna `TAB5_ERR_INVALID_ARG` quando o module_inst é nulo,
   o buffer_ptr é 0 (com capacity > 0), o buffer_ptr > UINT32_MAX, ou a
   validação do endereço do app falha.
4. O símbolo `tab5_ui_textarea_copy_text` está registrado em `s_native_symbols`.
5. Regra geral: toda alocação `wasm_runtime_module_malloc` no ABI de
   exportação tem um caminho de liberação correspondente.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ABI = (ROOT / "components/os/runtime/tab5_host_abi.cpp").read_text(encoding="utf-8")
RUNTIME_SDK = (ROOT / "components/os/runtime/include/tab5_sdk.h").read_text(encoding="utf-8")
SDK_SDK = (ROOT / "sdk/tab5-app-sdk/include/tab5_sdk.h").read_text(encoding="utf-8")

COPY_SYMBOL = "tab5_ui_textarea_copy_text"
COPY_WRAPPER = "wasm_tab5_ui_textarea_copy_text"


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for pos in range(opening, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : pos]
    raise AssertionError(f"função não fechada: {signature}")


class WasmCopyTextNoAllocationContract(unittest.TestCase):
    """wasm_tab5_ui_textarea_copy_text não pode alocar no heap do módulo WASM."""

    def setUp(self):
        self.body = function_body(ABI, f"static int32_t {COPY_WRAPPER}(")

    def test_wrapper_nao_usa_module_malloc(self):
        """copy_text copia para buffer do caller (addr_app_to_native) — não
        pode usar wasm_runtime_module_malloc, que cresce o heap do módulo."""
        self.assertNotIn(
            "wasm_runtime_module_malloc", self.body,
            f"{COPY_WRAPPER} contém wasm_runtime_module_malloc — "
            "cada chamada cresce o heap do módulo WASM sem limite. "
            "copy_text deve copiar para o buffer do app via "
            "wasm_runtime_addr_app_to_native.",
        )

    def test_wrapper_valida_endereco_app_antes_da_conversao(self):
        """O wrapper deve chamar wasm_runtime_validate_app_addr antes de
        wasm_runtime_addr_app_to_native (defesa contra endereços inválidos)."""
        self.assertIn(
            "wasm_runtime_validate_app_addr", self.body,
            f"{COPY_WRAPPER} precisa validar o endereço do app com "
            "wasm_runtime_validate_app_addr antes de converter.",
        )
        self.assertIn(
            "wasm_runtime_addr_app_to_native", self.body,
            f"{COPY_WRAPPER} precisa traduzir o endereço com "
            "wasm_runtime_addr_app_to_native.",
        )
        # A validação deve vir ANTES da conversão.
        idx_validate = self.body.index("wasm_runtime_validate_app_addr")
        idx_convert = self.body.index("wasm_runtime_addr_app_to_native")
        self.assertLess(
            idx_validate, idx_convert,
            "wasm_runtime_validate_app_addr deve vir ANTES de "
            "wasm_runtime_addr_app_to_native",
        )

    def test_wrapper_retorna_invalid_arg_para_module_inst_nulo(self):
        """Quando wasm_runtime_get_module_inst retorna nullptr, o wrapper
        retorna TAB5_ERR_INVALID_ARG sem crash."""
        self.assertRegex(
            self.body,
            r"module_inst\s*==\s*nullptr",
            f"{COPY_WRAPPER} precisa verificar module_inst == nullptr "
            "e retornar TAB5_ERR_INVALID_ARG.",
        )

    def test_wrapper_retorna_invalid_arg_para_buffer_nulo_com_capacity(self):
        """buffer_ptr == 0 com capacity > 0 deve retornar erro."""
        self.assertRegex(
            self.body,
            r"capacity\s*>\s*0\s*&&\s*buffer_ptr\s*==\s*0",
            f"{COPY_WRAPPER} precisa retornar TAB5_ERR_INVALID_ARG quando "
            "buffer_ptr == 0 e capacity > 0.",
        )

    def test_wrapper_trunca_buffer_ptr_grande(self):
        """buffer_ptr > UINT32_MAX deve retornar erro (overflow de 64->32 bits)."""
        self.assertIn(
            "UINT32_MAX", self.body,
            f"{COPY_WRAPPER} precisa validar buffer_ptr <= UINT32_MAX "
            "antes de truncar para uint32_t.",
        )


class WasmCopyTextSymbolRegistrationContract(unittest.TestCase):
    """tab5_ui_textarea_copy_text está registrado na tabela de símbolos do ABI."""

    def test_simbolo_registrado_em_s_native_symbols(self):
        self.assertRegex(
            ABI,
            r'\{"' + re.escape(COPY_SYMBOL) + r'"\s*,\s*\(void\s*\*\)\s*'
            + re.escape(COPY_WRAPPER) + r"\s*,",
            f"{COPY_SYMBOL} precisa estar registrado em s_native_symbols "
            f"apontando para {COPY_WRAPPER}",
        )

    def test_symbol_declared_in_runtime_sdk(self):
        self.assertIn(
            COPY_SYMBOL, RUNTIME_SDK,
            f"runtime/include/tab5_sdk.h precisa declarar {COPY_SYMBOL} "
            "para que o app WASM possa chamá-lo.",
        )
        decl = RUNTIME_SDK[RUNTIME_SDK.index(COPY_SYMBOL):]
        self.assertRegex(
            decl, r"\([^)]*\)\s*;",
            f"runtime/include/tab5_sdk.h precisa ter a assinatura de {COPY_SYMBOL}",
        )

    def test_symbol_declared_in_sdk_sdk(self):
        self.assertIn(
            COPY_SYMBOL, SDK_SDK,
            f"sdk/tab5-app-sdk/include/tab5_sdk.h precisa declarar {COPY_SYMBOL} "
            "para que o app WASM possa chamá-lo.",
        )
        decl = SDK_SDK[SDK_SDK.index(COPY_SYMBOL):]
        self.assertRegex(
            decl, r"\([^)]*\)\s*;",
            f"sdk/tab5-app-sdk/include/tab5_sdk.h precisa ter a assinatura "
            f"de {COPY_SYMBOL}",
        )

    def test_no_stale_get_text_in_native_symbols(self):
        """O velho símbolo get_text não pode estar mais na tabela — foi
        substituído por copy_text que resolve o problema de ownership."""
        # Procura a entrada get_text em s_native_symbols (excluindo a
        # definição de typedef/typedef).
        in_symbols_section = ABI[ABI.index("s_native_symbols"):]
        self.assertNotIn(
            '"tab5_ui_textarea_get_text"',
            in_symbols_section,
            "tab5_ui_textarea_get_text ainda está em s_native_symbols — "
            f"deve ser substituído por {COPY_SYMBOL}.",
        )


class WasmModuleHeapBoundedContract(unittest.TestCase):
    """Regra geral: toda alocação de módulo no ABI tem liberação pareada."""

    @unittest.expectedFailure
    def test_todo_module_malloc_de_exportacao_tem_release(self):
        """Todas as alocações do ABI de exportação precisam de uma forma de
        liberação. copy_text não aloca; os allocations restantes (ai_get_response,
        ai_get_error) devem ter liberação correspondente.

        NOTA TDD: esta falha é esperada (xfail). os wrappers wasm_tab5_ai_get_response
        e wasm_tab5_ai_get_error em tab5_host_abi.cpp ainda usam
        wasm_runtime_module_malloc para copiar strings ao heap do módulo sem
        símbolo pareado de liberação. copy_text já resolveu o problema de
        textarea; estes dois wrappers ainda não. Produza o fix de produção
        para remover este xfail.
        """
        mallocs = len(re.findall(r"wasm_runtime_module_malloc\s*\(", ABI))
        frees = len(re.findall(r"wasm_runtime_module_free\s*\(", ABI))
        self.assertGreaterEqual(
            frees, 1,
            f"nenhum wasm_runtime_module_free no ABI — alocações de string "
            f"para o WASM podem vazar ({mallocs} module_malloc sem free)",
        )
        self.assertGreaterEqual(
            mallocs, 1,
            "sem alocações de módulo no ABI (sanidade do próprio contrato)",
        )
        # copy_text não deve contribuir para o count de module_malloc.
        copy_body = function_body(ABI, f"static int32_t {COPY_WRAPPER}(")
        self.assertNotIn(
            "wasm_runtime_module_malloc", copy_body,
            "copy_text não pode conter wasm_runtime_module_malloc",
        )


if __name__ == "__main__":
    unittest.main()
