"""Contratos: Terminal sem barra rápida + Enter/READY submit + prompt preservado + clear mantém prompt.

Objetivo confirmado (plano aprovado):

1. **Sem barra rápida**: o app Terminal NÃO deve criar botões de ação na app
   bar via `tab5_ui_app_bar_add_action_button`. Os comandos help/ls/free/df/
   date/clear devem ser acessíveis apenas digitando no textarea + Enter/READY.

2. **Enter/READY executa e renderiza retorno**: ao submeter com Enter
   (VALUE_CHANGED com last_char == '\\n') ou READY, o texto digitado passa
   por `execute_command`, que invoca `tab5_terminal_exec` e renderiza a saída
   no textarea (concatenação em `s_term_history`).

3. **Prompt preservado**: após qualquer execução (inclusive erro e comando vazio),
   o prompt `"\\n" + cwd + " $ "` (ou `cwd + " $ "`) permanece visível no final
   de `s_term_history` e `s_prompt_min_index` aponta para o início dele.

4. **Clear mantém prompt**: o comando `clear` reseta `s_term_history` mas
   reconstrói o prompt `cwd + " $ "`, atualiza `s_prompt_min_index`, atualiza a UI
   e retorna imediatamente sem invocar comandos externos.

Nível: source (contrato estático no fonte C).
Política: NÃO altera código de produção; testes que dependem da implementação
(barra rápida removida) vão falhar com assert claro antes da correção.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAIN_C = (ROOT.parent / "tab5-app-terminal/src/main.c").read_text(encoding="utf-8")


def strip_c_comments_and_strings(source: str) -> str:
    """Remove comentários C (// e /* ... */) e literais de string/char, preservando quebras de linha."""
    result = []
    i = 0
    n = len(source)
    while i < n:
        if source[i : i + 2] == "//":
            # Pula linha até newline
            end = source.find("\n", i)
            if end == -1:
                break
            result.append("\n")
            i = end + 1
        elif source[i : i + 2] == "/*":
            # Pula comentário de bloco
            end = source.find("*/", i + 2)
            if end == -1:
                break
            newlines = source[i : end + 2].count("\n")
            result.append("\n" * newlines)
            i = end + 2
        elif source[i] == '"':
            # Pula string literal
            i += 1
            while i < n and source[i] != '"':
                if source[i] == "\\" and i + 1 < n:
                    i += 2
                else:
                    i += 1
            i += 1
            result.append('""')
        elif source[i] == "'":
            # Pula char literal
            i += 1
            while i < n and source[i] != "'":
                if source[i] == "\\" and i + 1 < n:
                    i += 2
                else:
                    i += 1
            i += 1
            result.append("''")
        else:
            result.append(source[i])
            i += 1
    return "".join(result)


def function_body(source: str, signature: str) -> str:
    """Extrai o corpo de uma função a partir de sua assinatura, respeitando strings e comentários."""
    sig_index = source.find(signature)
    if sig_index == -1:
        raise AssertionError(f"Assinatura não encontrada: {signature}")

    # Encontra a primeira abertura de bloco { após a assinatura
    opening = source.find("{", sig_index)
    if opening == -1:
        raise AssertionError(f"Abertura de função não encontrada: {signature}")

    # Contagem de chaves ciente de strings/chars/comentários
    depth = 0
    i = opening
    n = len(source)
    in_line_comment = False
    in_block_comment = False
    in_string = False
    in_char = False

    while i < n:
        c = source[i]
        next_c = source[i + 1] if i + 1 < n else ""

        if in_line_comment:
            if c == "\n":
                in_line_comment = False
        elif in_block_comment:
            if c == "*" and next_c == "/":
                in_block_comment = False
                i += 1
        elif in_string:
            if c == "\\" and next_c:
                i += 1
            elif c == '"':
                in_string = False
        elif in_char:
            if c == "\\" and next_c:
                i += 1
            elif c == "'":
                in_char = False
        else:
            if c == "/" and next_c == "/":
                in_line_comment = True
                i += 1
            elif c == "/" and next_c == "*":
                in_block_comment = True
                i += 1
            elif c == '"':
                in_string = True
            elif c == "'":
                in_char = True
            elif c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    return source[opening + 1 : i]
        i += 1

    raise AssertionError(f"Função não fechada: {signature}")


def extract_block_after(source: str, start_pattern: str) -> str:
    """Extrai o bloco entre { e } imediatamente após um determinado padrão."""
    match = re.search(start_pattern, source)
    if not match:
        raise AssertionError(f"Padrão de início não encontrado: {start_pattern}")
    start_pos = match.end()
    opening = source.find("{", start_pos)
    if opening == -1:
        raise AssertionError(f"Chave de abertura não encontrada após: {start_pattern}")

    depth = 0
    i = opening
    n = len(source)
    in_line_comment = False
    in_block_comment = False
    in_string = False
    in_char = False

    while i < n:
        c = source[i]
        next_c = source[i + 1] if i + 1 < n else ""

        if in_line_comment:
            if c == "\n":
                in_line_comment = False
        elif in_block_comment:
            if c == "*" and next_c == "/":
                in_block_comment = False
                i += 1
        elif in_string:
            if c == "\\" and next_c:
                i += 1
            elif c == '"':
                in_string = False
        elif in_char:
            if c == "\\" and next_c:
                i += 1
            elif c == "'":
                in_char = False
        else:
            if c == "/" and next_c == "/":
                in_line_comment = True
                i += 1
            elif c == "/" and next_c == "*":
                in_block_comment = True
                i += 1
            elif c == '"':
                in_string = True
            elif c == "'":
                in_char = True
            elif c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    return source[opening + 1 : i]
        i += 1

    raise AssertionError(f"Bloco não fechado após: {start_pattern}")


# ---------------------------------------------------------------------------
# 1. Sem barra rápida
# ---------------------------------------------------------------------------

class TerminalNoQuickBarContract(unittest.TestCase):
    """O Terminal não deve criar botões de ação (barra rápida) na app bar."""

    def test_nao_chama_app_bar_add_action_button(self):
        """Nenhuma chamada a tab5_ui_app_bar_add_action_button no fonte."""
        matches = re.findall(
            r"tab5_ui_app_bar_add_action_button\s*\(", MAIN_C
        )
        self.assertEqual(
            len(matches), 0,
            f"Terminal ainda chama tab5_ui_app_bar_add_action_button "
            f"{len(matches)} vez(es) — a barra rápida deve ser removida "
            "conforme o objetivo confirmado",
        )

    def test_nao_declara_s_quick_buttons(self):
        """Nenhum array s_quick_buttons nem s_quick_commands no fonte."""
        self.assertNotIn("s_quick_buttons", MAIN_C,
                         "s_quick_buttons deve ser removido (sem barra rápida)")
        self.assertNotIn("s_quick_commands", MAIN_C,
                         "s_quick_commands deve ser removido (sem barra rápida)")

    def test_nao_itera_quick_buttons_no_event_handler(self):
        """Nenhum loop sobre s_quick_buttons no on_ui_event."""
        body = function_body(MAIN_C, "tab5_app_on_ui_event(")
        self.assertNotIn("s_quick_buttons", body,
                         "on_ui_event ainda itera sobre s_quick_buttons")


# ---------------------------------------------------------------------------
# 2. Enter/READY executa e renderiza retorno
# ---------------------------------------------------------------------------

class TerminalEnterReadySubmitContract(unittest.TestCase):
    """Enter (VALUE_CHANGED com last_char=='\\n') e READY chamam execute_command."""

    def test_value_changed_verifica_last_char_newline(self):
        """VALUE_CHANGED compara last_char com '\\n' antes de chamar execute_command."""
        body = function_body(MAIN_C, "tab5_app_on_ui_event(")
        self.assertTrue(
            re.search(r"last_char\s*==\s*'\\n'|last_char\s*==\s*10|last_char\s*==\s*'\\r'", body),
            "VALUE_CHANGED precisa detectar last_char == '\\n' para submeter via Enter",
        )

    def test_value_changed_chama_execute_command(self):
        """Dentro do bloco VALUE_CHANGED, ao detectar newline, chama execute_command(NULL)."""
        body = function_body(MAIN_C, "tab5_app_on_ui_event(")
        vc_block = extract_block_after(body, r"event_type\s*==\s*TAB5_UI_EVENT_VALUE_CHANGED")
        self.assertTrue(
            re.search(r"execute_command\s*\(\s*NULL\s*\)", vc_block),
            "VALUE_CHANGED precisa invocar execute_command(NULL) ao receber Enter",
        )

    def test_ready_event_chama_execute_command(self):
        """EVENT_READY chama execute_command(NULL) dentro do seu próprio handler/bloco."""
        body = function_body(MAIN_C, "tab5_app_on_ui_event(")
        self.assertIn("TAB5_UI_EVENT_READY", body,
                      "on_ui_event precisa tratar TAB5_UI_EVENT_READY")
        # Isola a ocorrência do ramo READY garantindo que execute_command está vinculado a ele
        ready_match = re.search(
            r"TAB5_UI_EVENT_READY\s*[\):]?[^;{}]*\{?([^;]*execute_command\s*\(\s*NULL\s*\))",
            body,
            re.DOTALL,
        )
        self.assertIsNotNone(
            ready_match,
            "EVENT_READY precisa invocar explicitamente execute_command(NULL) no seu escopo",
        )

    def test_execute_command_invoca_terminal_exec(self):
        """execute_command chama tab5_terminal_exec para executar comandos não vazios."""
        body = function_body(MAIN_C, "static void execute_command(")
        self.assertIn("tab5_terminal_exec(", body,
                      "execute_command precisa invocar tab5_terminal_exec")

    def test_execute_command_renderiza_saida_no_textarea(self):
        """execute_command concatena a saída em s_term_history e atualiza textarea."""
        body = function_body(MAIN_C, "static void execute_command(")
        self.assertIn("s_term_history", body,
                      "execute_command precisa usar s_term_history")
        self.assertIn("tab5_ui_textarea_set_text", body,
                      "execute_command precisa chamar tab5_ui_textarea_set_text")

    def test_execute_command_sanitiza_quebra_de_linha_do_input(self):
        """execute_command remove quebras de linha ('\\n', '\\r') residuais do comando."""
        body = function_body(MAIN_C, "static void execute_command(")
        self.assertTrue(
            re.search(r"['\"]\\n['\"]|['\"]\\r['\"]", body),
            "execute_command precisa remover '\\n' e '\\r' do comando antes da execução",
        )

    def test_execute_command_protecao_reentrancia(self):
        """execute_command utiliza guard de reentrância (ex: s_is_processing_cmd)."""
        body = function_body(MAIN_C, "static void execute_command(")
        self.assertIn("s_is_processing_cmd", body,
                      "execute_command precisa utilizar flag de proteção contra reentrância")


# ---------------------------------------------------------------------------
# 3. Prompt preservado
# ---------------------------------------------------------------------------

class TerminalPromptPreservedContract(unittest.TestCase):
    """Após execução (inclusive erro e linha vazia), o prompt permanece no final."""

    def test_execute_command_acrescenta_prompt_ao_final(self):
        """Ao final de execute_command, s_current_cwd e ' $ ' são acrescentados."""
        body = function_body(MAIN_C, "static void execute_command(")
        self.assertTrue(
            re.search(
                r"strcat\s*\(\s*s_term_history\s*,\s*s_current_cwd\s*\)|"
                r"strncat\s*\(\s*s_term_history\s*,\s*s_current_cwd|"
                r"snprintf\s*\([^,]+,\s*[^,]+,\s*.*s_current_cwd",
                body,
            ),
            "execute_command precisa concatenar s_current_cwd no prompt final",
        )
        self.assertTrue(
            re.search(r'"\s*\$\s*"', body),
            "execute_command precisa incluir ' $ ' no prompt final",
        )

    def test_execute_command_preserva_prompt_em_caso_de_erro(self):
        """execute_command trata retorno de erro de tab5_terminal_exec mantendo o fluxo até o prompt."""
        body = function_body(MAIN_C, "static void execute_command(")
        self.assertTrue(
            re.search(r"err\s*!=\s*TAB5_OK|err\s*==\s*TAB5_OK", body),
            "execute_command precisa verificar o código de retorno de tab5_terminal_exec",
        )

    def test_s_prompt_min_index_aponta_para_inicio_do_prompt(self):
        """s_prompt_min_index é atualizado com strlen(s_term_history) ao final."""
        body = function_body(MAIN_C, "static void execute_command(")
        self.assertTrue(
            re.search(
                r"s_prompt_min_index\s*=\s*strlen\s*\(\s*s_term_history\s*\)",
                body,
            ),
            "s_prompt_min_index precisa ser atualizado com strlen(s_term_history) ao final de execute_command",
        )

    def test_init_constroi_prompt_com_cwd(self):
        """app_init constrói o prompt inicial com cwd + ' $ '."""
        body = function_body(MAIN_C, "app_init(")
        self.assertIn("s_current_cwd", body,
                      "app_init precisa usar s_current_cwd no prompt inicial")
        self.assertTrue(
            re.search(r'"\s*\$\s*"', body),
            "app_init precisa incluir ' $ ' no prompt inicial",
        )

    def test_readonly_hist_protegido_contracampo(self):
        """Após VALUE_CHANGED, verifica integridade do prompt com strncmp e restaura se corrompido."""
        body = function_body(MAIN_C, "tab5_app_on_ui_event(")
        self.assertTrue(
            re.search(
                r"strncmp\s*\(\s*txt\s*,\s*s_term_history\s*,\s*s_prompt_min_index\s*\)",
                body,
            ),
            "on_ui_event precisa validar se o início do texto coincide com s_term_history até s_prompt_min_index",
        )


# ---------------------------------------------------------------------------
# 4. Clear mantém prompt
# ---------------------------------------------------------------------------

class TerminalClearPreservesPromptContract(unittest.TestCase):
    """O comando 'clear' reseta s_term_history mas reconstrói o prompt."""

    def test_clear_detectado_por_strcmp(self):
        """execute_command detecta 'clear' via strcmp ou equivalente."""
        body = function_body(MAIN_C, "static void execute_command(")
        self.assertTrue(
            re.search(
                r'strcmp\s*\(\s*input_line\s*,\s*"clear"\s*\)\s*==\s*0|'
                r'!\s*strcmp\s*\(\s*input_line\s*,\s*"clear"\s*\)|'
                r'strcmp\s*\(\s*"clear"\s*,\s*input_line\s*\)\s*==\s*0',
                body,
            ),
            "comando 'clear' deve ser detectado via strcmp(input_line, \"clear\")",
        )

    def test_clear_bloco_reseta_e_reconstroi_prompt_completamente(self):
        """O bloco 'clear' reseta histórico, reconstrói prompt, atualiza min_index, UI e retorna."""
        body = function_body(MAIN_C, "static void execute_command(")
        clear_block = extract_block_after(body, r'strcmp\s*\([^)]*"clear"[^)]*\)')

        # 1. Reseta histórico
        self.assertTrue(
            re.search(
                r"s_term_history\s*\[\s*0\s*\]\s*=\s*(0|'\\0'|'\\0')|"
                r"memset\s*\(\s*s_term_history|"
                r"strcpy\s*\(\s*s_term_history\s*,\s*\"\"",
                clear_block,
            ),
            "bloco clear precisa resetar s_term_history para string vazia",
        )

        # 2. Reconstrói prompt com cwd
        self.assertIn("s_current_cwd", clear_block,
                      "bloco clear precisa incluir s_current_cwd no prompt reconstruído")
        self.assertTrue(
            re.search(r'"\s*\$\s*"', clear_block),
            "bloco clear precisa incluir ' $ ' no prompt reconstruído",
        )

        # 3. Atualiza s_prompt_min_index
        self.assertTrue(
            re.search(
                r"s_prompt_min_index\s*=\s*strlen\s*\(\s*s_term_history\s*\)",
                clear_block,
            ),
            "bloco clear precisa atualizar s_prompt_min_index dentro do seu escopo",
        )

        # 4. Atualiza textarea e cursor
        self.assertIn("tab5_ui_textarea_set_text", clear_block,
                      "bloco clear precisa chamar tab5_ui_textarea_set_text")
        self.assertIn("TAB5_UI_CURSOR_LAST", clear_block,
                      "bloco clear precisa posicionar o cursor no final com TAB5_UI_CURSOR_LAST")

        # 5. Early return para não executar terminal_exec
        self.assertTrue(
            re.search(r"return\s*;", clear_block),
            "bloco clear precisa encerrar com 'return;' para evitar invocar tab5_terminal_exec",
        )


# ---------------------------------------------------------------------------
# 5. README atualizado (sem menção à barra rápida)
# ---------------------------------------------------------------------------

class TerminalReadmeContract(unittest.TestCase):
    """README não deve mais mencionar a barra rápida."""

    @classmethod
    def setUpClass(cls):
        readme_path = ROOT.parent / "tab5-app-terminal/README.md"
        if not readme_path.exists():
            raise unittest.SkipTest(f"README ausente: {readme_path}")
        cls.text = readme_path.read_text(encoding="utf-8")

    def test_nao_menciona_barra_rapida(self):
        """README não deve mencionar 'barra rápida'."""
        self.assertNotIn(
            "barra rápida", self.text.lower(),
            "README ainda menciona 'barra rápida' — deve ser atualizado "
            "após remoção dos botões de ação",
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
