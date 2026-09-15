"""Contratos do Micro-Shell: comandos free/df/date e fallback host.

O Micro-Shell (`components/os/core/terminal_cmd.cpp`) implementa
ls/cd/pwd/mkdir/rm/rrmdir/touch/cat/echo/ssh/clear/whoami/uname/help. O plano
adiciona os comandos `free`, `df` e `date` e exige que o `help` os liste.
Em paralelo, o despacho ABI `tab5_terminal_exec` em build de host (não-ESP —
ex.: simulador/serial bridge) usa hoje um stub genérico `"Output of '%s'"`,
que impede a validação real do Micro-Shell no dispositivo simulado.

Contratos (level: source — comportamento dinâmico coberto pelo gtest
test_terminal_micro_shell.cpp):

1. `terminal_exec` despacha `free`, `df` e `date` para funções próprias
   (`cmd_free`, `cmd_df`, `cmd_date`).
2. O help lista: ls, cd, pwd, free, df, date, clear, help (núcleo do plano).
3. Saídas mínimas válidas:
   - `cmd_free`: contém dígitos (valores de memória) e título de seção.
   - `cmd_df`: referencia o caminho alvo (statvfs/getcwd).
   - `cmd_date`: formata a hora com strftime/localtime (compatível com o
     formato validado no gtest).
4. o dispatcher de `tab5_terminal_exec` (todas as branches) NÃO pode usar o
   stub `"Output of '%s'"` — todas as builds (ESP e host) devem rotear para
   `terminal_exec`.

Gtest complementar: tests/host/src/test_terminal_micro_shell.cpp.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CMD = (ROOT / "components/os/core/terminal_cmd.cpp").read_text(encoding="utf-8")
ABI = (ROOT / "components/os/runtime/tab5_host_abi.cpp").read_text(encoding="utf-8")


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


CORE_COMMANDS = ("help", "ls", "cd", "pwd", "clear")
SHELL_COMMANDS = CORE_COMMANDS + ("free", "df", "date")


class TermCmdDispatchContract(unittest.TestCase):
    """Dispatch de free/df/date no terminal_exec e presença das funções."""

    def test_terminal_exec_despacha_free_df_date(self):
        body = function_body(CMD, "std::string terminal_exec(")
        for cmdname in ("free", "df", "date"):
            with self.subTest(cmd=cmdname):
                self.assertTrue(
                    re.search(
                        r'(?<![\w])cmd\s*==\s*"' + cmdname + r'"(?![\w])|'
                        r'(?<![\w])cmd\.compare\s*\(\s*"' + cmdname + r'"\s*\)\s*==\s*0',
                        body,
                    ),
                    f"terminal_exec precisa despachar '{cmdname}'",
                )

    def test_funcoes_cmd_free_df_date_existem(self):
        for fn in ("std::string cmd_free", "std::string cmd_df", "std::string cmd_date"):
            with self.subTest(fn=fn):
                self.assertIn(fn, CMD, f"função {fn} ausente em terminal_cmd.cpp")

    def test_cmd_free_reporta_tamanhos_com_digitos(self):
        body = function_body(CMD, "std::string cmd_free(")
        self.assertRegex(
            body, r"%\s*[0-9dlLu]+|std::to_string\s*\(",
            "cmd_free precisa renderizar valores numéricos",
        )
        self.assertTrue(
            re.search(r"(?i)mem|free|used|total|heap", body),
            "cmd_free precisa nomear as seções de memória",
        )

    def test_cmd_df_referencia_caminho_alvo(self):
        body = function_body(CMD, "std::string cmd_df(")
        self.assertRegex(
            body, r"statvfs\s*\(|getcwd\s*\(|path\s*=|target\s*=|\.c_str\(\)",
            "cmd_df precisa consultar o filesystem no caminho alvo",
        )

    def test_cmd_date_formata_hora(self):
        body = function_body(CMD, "std::string cmd_date(")
        self.assertRegex(
            body, r"strftime\s*\(|asctime\s*\(|localtime\s*\(|gmtime\s*\(|get_localtime",
            "cmd_date precisa formatar data/hora via API de tempo",
        )


class TermCmdHelpContract(unittest.TestCase):
    """O help do Micro-Shell lista o núcleo do plano (incl. free/df/date)."""

    def test_help_lista_todos_os_comandos_core_do_shell(self):
        body = function_body(CMD, "std::string cmd_help(")
        for cmdname in SHELL_COMMANDS:
            with self.subTest(cmd=cmdname):
                # O texto de ajuda alinha os comandos com dois espaços após a
                # abertura da string literal ("  ls [caminho] ..."), então o
                # nome não pode ser casado com a aspa colada no comando.
                self.assertRegex(
                    body,
                    r'"\s*' + re.escape(cmdname) + r'\b',
                    f"cmd_help precisa listar '{cmdname}' no texto de ajuda",
                )

    def test_help_nao_regressa_overview_da_pipeline(self):
        body = function_body(CMD, "std::string cmd_help(")
        self.assertGreater(
            len(body), 400,
            "help perdeu o texto de ajuda (superficial demais p/ os comandos)",
        )


class Tab5TerminalExecHostFallbackContract(unittest.TestCase):
    """Nenhuma branch do dispatcher ABI pode usar o stub genérico."""

    def test_sem_stub_output_of(self):
        self.assertNotIn(
            '"Output of \'%s\'"', ABI,
            "tab5_terminal_exec host (não-ESP) usa stub genérico "
            "\"Output of '%s'\" — o fallback precisa rotear para terminal_exec "
            "para o Micro-Shell funcionar em sim/serial/device-test",
        )

    def test_todas_as_branches_chamam_terminal_exec(self):
        body = function_body(ABI, "tab5_err_t tab5_terminal_exec(")
        self.assertIn("terminal_exec(", body)

    def test_esp_branch_trunca_com_nul(self):
        body = function_body(ABI, "tab5_err_t tab5_terminal_exec(")
        self.assertRegex(
            body, r"out_buf\[[^\]]*\]\s*=\s*'\\0'|\[\s*buf_size\s*-\s*1\s*\]\s*=\s*0",
            "a branch ESP precisa NULL-terminar out_buf",
        )


if __name__ == "__main__":
    unittest.main()
