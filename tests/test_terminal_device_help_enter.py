#!/usr/bin/env python3
"""Validação física (device-in-the-loop) do fluxo `help` + Enter no Terminal.

Faz o mesmo percurso do bug reportado (M5Stack Tab5: `help` + Enter trava o
Terminal) DIRETO no hardware, via Serial Automation Bridge (boneco-de-lata.md):

  1. Abre com.tab5.terminal e confirma via app.active (o bridge ativo).
  2. Localiza o textarea no ui.dump (item de maior área com texto).
   3. Digita "help" + Enter (ui.type) — executa 3 vezes para detectar vazamentos.
  4. Após CADA round, sonda sys.info: se o device não responder — travamento
     real reproduzido (falha). Registra heap_free_internal/heap_free_psram
     como evidência (sem asserção: a RAM do host pode não refletir o heap do
     módulo WASM).
  5. Verifica que a tela mudou (ui.dump) e contém o texto do help
     ("Tab5 OS Terminal Shell") e, ao final, confere app.close + sys.info (apenas
     limpeza: o device NÃO é reiniciado/flasheado — só o app é fechado).

Independência entre testes: CADA teste que exige o Terminal abre
`com.tab5.terminal`, aguarda o settle do launch e confirma `app.active`
(active=true com o id do Terminal) ANTES de qualquer ui.dump/foco/type.
Nenhum método herda estado (open/foco/typing) dos anteriores — a suíte pode
rodar inteira, parcialmente ou em qualquer ordem sem mudar o que cada teste
mede. O test_00 permanece o teste original de abertura (referência).

Política: a suíte PULA inteira (com mensagem de blocker no transcript) quando
a porta não existe ou o bridge não responde sys.info. Timeouts curtos em cada
exchange para não travar a suíte caso o device congele.

Falha pré-implementação esperada: travamento/instabilidade OU texto de help
sem o header do Micro-Shell — documentado no transcript para o handoff.
"""

import json
import os
import sys
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from test_serial_bridge_device_validation import (  # noqa: E402
    DEVICE_BAUD,
    DEVICE_PORT,
    OUT_ROOT,
    SerialBridgeTransport,
)

TERMINAL_APP_ID = "com.tab5.terminal"
HELP_HEADER_HINT = "Tab5 OS Terminal Shell"
ROUNDS = 3
RESPONSIVE_TIMEOUT_S = 12  # rápido: travamento => falha em ~12s, não 120s
# Margem para o launch assíncrono do WASM do Terminal: o firmware às vezes
# responde app.active com active=false logo após app.open (launch ainda em
# andamento). O helper aguarda e re-confirma com settle entre tentativas.
ACTIVE_MAX_ATTEMPTS = 3
ACTIVE_RETRY_SETTLE_S = 1.5


def largest_text_item(frames):
    """Item do dump com o maior volume de texto — o terminal textarea.

    O dump não classifica widgets (só x/y/w/h/text), então o identificador
    confiável do textarea é a quantidade de texto: o histórico+prompt já tem
    ~90 chars na tela inicial, contra 1–9 chars dos glyphs/título da app bar
    (e ~16 chars do relógio da status bar). Área (w*h) é o desempate entre os
    dois retângulos do textarea (objeto e texto visível). Um corte posicional
    fixo (ex.: y<120) estava excluindo o próprio alvo no device: tanto a app
    bar quanto o textarea ficam no topo (o textarea começa em 2*UI_BAR_HEIGHT
    ≈ 104). Importante: este helper só é confiável quando o Terminal está
    confirmado ativo (ensure_terminal_active) — com o launcher em primeiro
    plano o "maior texto" vira o relógio da status bar e o clique iria para
    fora do textarea.
    """
    if not frames:
        return None
    data = frames[0].get("data") or {}
    items = data.get("items") or []
    best, best_key = None, (-1, -1)
    for it in items:
        if not isinstance(it, dict):
            continue
        text = it.get("text") or ""
        w, h = it.get("w") or 0, it.get("h") or 0
        if not text.strip() or w <= 0 or h <= 0:
            continue
        key = (len(text), w * h)
        if key > best_key:
            best, best_key = it, key
    return best


class TerminalDeviceHelpEnterValidation(unittest.TestCase):
    """help+Enter no hardware: responsividade, tela, vazamento e limpeza."""

    findings = []

    @classmethod
    def setUpClass(cls):
        if not Path(DEVICE_PORT).exists():
            raise unittest.SkipTest(
                f"[BLOCKER] porta do device ausente: {DEVICE_PORT} — validação "
                f"física do Terminal não executada")
        cls.out = OUT_ROOT / f"terminal_help_enter_{time.strftime('%Y%m%d_%H%M%S')}"
        cls.out.mkdir(parents=True, exist_ok=True)
        cls.transcript = open(cls.out / "transcript.ndjson", "w", encoding="utf-8")
        os.environ["TAB5_DEVICE_PORT"] = DEVICE_PORT
        try:
            cls.transport = SerialBridgeTransport(DEVICE_PORT, DEVICE_BAUD, cls.transcript)
        except Exception as exc:
            cls.transcript.write(json.dumps(
                {"ts": time.strftime("%Y-%m-%dT%H:%M:%S"),
                 "blocker": f"abertura da porta falhou: {exc!r}"}) + "\n")
            cls.transcript.close()
            raise unittest.SkipTest(f"não foi possível abrir {DEVICE_PORT}: {exc!r}")
        probe = cls.transport.exchange('{"cmd":"sys.info"}', timeout_s=12,
                                       label="probe sys.info")
        if not probe:
            cls.transcript.write(json.dumps(
                {"ts": time.strftime("%Y-%m-%dT%H:%M:%S"),
                 "blocker": "bridge não respondeu sys.info — validação física não executada"}) + "\n")
            cls.transcript.close()
            raise unittest.SkipTest(
                f"bridge não respondeu sys.info em {DEVICE_PORT}")
        cls.sysinfo = probe[0]

    @classmethod
    def tearDownClass(cls):
        # Limpeza SEM derrubar o device: apenas fecha o app (nunca reinicia,
        # flasheia ou apaga o dispositivo) e sonda sys.info para registrar
        # (sem asserção) se o device seguiu responsivo após o cleanup.
        try:
            cls.transport.settle(0.5)
            cls.transport.exchange('{"cmd":"app.close"}', timeout_s=8,
                                   label="final app.close")
        except Exception:
            pass
        try:
            cls.transport.settle(0.5)
            alive = cls.transport.exchange('{"cmd":"sys.info"}', timeout_s=8,
                                           label="probe pós-cleanup sys.info")
            cls.record_finding(
                "info" if alive else "falha",
                "cleanup",
                f"device segue responsivo após app.close"
                if alive else "device NÃO respondeu sys.info após app.close — "
                "terminal travado/instável pós help+Enter (bug reproduzido)")
        except Exception as exc:
            cls.record_finding("falha", "cleanup",
                               f"exceção ao sondar após app.close: {exc!r}")
        cls.transcript.write(json.dumps(
            {"ts": time.strftime("%Y-%m-%dT%H:%M:%S"),
             "findings": cls.findings}, ensure_ascii=False) + "\n")
        cls.transcript.close()

    @classmethod
    def record_finding(cls, level, scope, message):
        cls.findings.append({"level": level, "scope": scope, "message": message})
        print(f"[FINDING {level}] {scope}: {message}", flush=True)

    def responsive(self, label):
        """sys.info (timeout curto): sobreviver = device não travou."""
        frames = self.transport.exchange('{"cmd":"sys.info"}', timeout_s=RESPONSIVE_TIMEOUT_S,
                                         label=label)
        if frames:
            data = frames[0].get("data") or {}
            self.record_finding(
                "info", "sys.info",
                f"{label}: heap_free_internal={data.get('heap_free_internal')} "
                f"psram={data.get('heap_free_psram')}")
        return frames

    def ensure_terminal_active(self, context):
        """Abre com.tab5.terminal, aguarda e confirma app.active.

        Setup obrigatório de TODO teste que exige o Terminal: abre o app,
        espera o settle do launch e confirma app.active(active=true) com o id
        do Terminal ANTES de qualquer ui.dump/foco. Se o device não confirmar
        em ACTIVE_MAX_ATTEMPTS tentativas (com settle entre elas), o teste
        falha com mensagem explícita — nunca prossegue cegamente para o dump/
        foco com o Terminal fora de primeiro plano (o que produziria falso
        alvo = relógio da status bar e falsos negativos).
        """
        self.transport.exchange(
            json.dumps({"cmd": "app.open", "id": TERMINAL_APP_ID}),
            timeout_s=15, label=f"app.open terminal ({context})")
        self.transport.settle(2.5)
        last_data = {}
        for attempt in range(1, ACTIVE_MAX_ATTEMPTS + 1):
            frames = self.transport.exchange(
                '{"cmd":"app.active"}', timeout_s=10,
                label=f"app.active após open ({context}, tentativa {attempt})")
            self.assertTrue(
                frames,
                f"app.active sem resposta após abrir o Terminal ({context})")
            last_data = frames[0].get("data") or {}
            if last_data.get("active") and last_data.get("id") == TERMINAL_APP_ID:
                self.record_finding(
                    "info", "app.active",
                    f"{context}: Terminal ativo (tentativa {attempt}, "
                    f"app_id={last_data.get('app_id')!r})")
                return frames
            if attempt < ACTIVE_MAX_ATTEMPTS:
                self.transport.settle(ACTIVE_RETRY_SETTLE_S)
        self.fail(
            f"{context}: Terminal NÃO confirmou app.active(true) em "
            f"{ACTIVE_MAX_ATTEMPTS} tentativas; última resposta: {last_data}. "
            f"ui.dump/foco não executados para não mirar alvo errado.")

    def focus_terminal_textarea(self):
        """Clica no centro do maior item textual para focar o textarea.

        O `ui.type` do bridge insere texto no objeto COM FOCO do grupo LVGL
        (lv_group_get_focused); o textarea do Terminal só entra no grupo (e
        passa a receber teclas) depois de um clique — é o `LV_EVENT_CLICKED`
        do host (`tab5_ui_host_create_app_screen`) que faz
        `lv_group_focus_obj` + `ui_keyboard_attach`. Sem o clique o texto
        digitado não chega ao Terminal (observado no device em 14/set/2026).
        Pré-condição: Terminal confirmado ativo (ensure_terminal_active) —
        caso contrário o "maior texto" pode ser o relógio da status bar.
        """
        dump = self.transport.exchange('{"cmd":"ui.dump"}', timeout_s=12,
                                       label="ui.dump (foco do textarea)")
        target = largest_text_item(dump)
        self.assertIsNotNone(
            target, f"textarget ausente para focar o textarea: {dump}")
        cx, cy = target["x"] + target["w"] // 2, target["y"] + target["h"] // 2
        click = self.transport.exchange(
            json.dumps({"cmd": "ui.click", "x": cx, "y": cy}), timeout_s=12,
            label=f"ui.click textarea em ({cx},{cy}) (foco/kb_target)")
        self.assertTrue(click, "ui.click no textarea não respondeu")
        self.assertEqual(click[0].get("status"), "ok",
                         f"ui.click no textarea falhou: {click[0]}")
        self.record_finding("info", "foco",
                            f"textarea clicado em ({cx},{cy}) — kb_target setado")
        self.transport.settle(1.0)

    def type_help_enter(self, round_label):
        """Digita help+Enter (ui.type) e confirma o device responsivo (sys.info).

        Retorna o frame do ui.type; falha com mensagens específicas se o
        ui.type não responder (device congelado) ou se o sys.info pós-comando
        não responder (trava real reproduzida).
        """
        self.record_finding("info", "help+Enter", f"{round_label}: digitando help")
        type_frames = self.transport.exchange(
            json.dumps({"cmd": "ui.type", "text": "help\n"}),
            timeout_s=RESPONSIVE_TIMEOUT_S, label=f"ui.type help\\n ({round_label})")
        self.assertTrue(
            type_frames,
            f"{round_label}: ui.type sem resposta — device congelado? "
            f"(provável reprodução do travamento)")
        self.transport.settle(1.5)
        alive = self.responsive(f"sys.info pós help+Enter ({round_label})")
        self.assertTrue(
            alive,
            f"{round_label}: sys.info não respondeu após help+Enter — "
            f"TERMINAL TRAVADO no dispositivo (bug reproduzido)")
        return type_frames

    def dump_texts(self, label):
        """Executa ui.dump e retorna o texto de todos os itens, unido."""
        dump = self.transport.exchange('{"cmd":"ui.dump"}', timeout_s=12, label=label)
        self.assertTrue(dump, f"{label}: ui.dump sem resposta")
        texts = [it.get("text", "")
                 for it in (dump[0].get("data") or {}).get("items") or []]
        return "\n".join(texts)

    # ------------------------------------------------------------------ tests
    def test_00_abre_terminal_e_confirma_foco(self):
        # Teste original, preservado: abre o Terminal e confirma que é o app
        # ativo. Os demais testes são AUTÔNOMOS (cada um abre/confirma o seu
        # próprio Terminal) e não dependem deste.
        self.transport.exchange(json.dumps({"cmd": "app.open", "id": TERMINAL_APP_ID}),
                                timeout_s=15, label="app.open terminal")
        self.transport.settle(2.5)
        frames = self.transport.exchange('{"cmd":"app.active"}', timeout_s=10,
                                         label="app.active após open")
        self.assertTrue(frames, "app.active sem resposta após abrir o Terminal")
        data = frames[0].get("data") or {}
        self.assertEqual(
            data.get("id"), TERMINAL_APP_ID,
            f"app.active devolveu {data} — o Terminal não está em primeiro plano")

    def test_01_encontra_o_textarea_no_dump(self):
        # Sua própria abertura + confirmação de active, depois o dump.
        self.ensure_terminal_active("teste 01")
        dump = self.transport.exchange('{"cmd":"ui.dump"}', timeout_s=12,
                                       label="ui.dump (textarea)")
        target = largest_text_item(dump)
        self.assertIsNotNone(
            target, f"nenhum item textual na região central do dump: {dump}")
        self.record_finding("info", "ui.dump",
                            f"textarget={target.get('text', '')[:80]!r} "
                            f"x={target.get('x')} y={target.get('y')} w={target.get('w')} h={target.get('h')}")
        # O bridge não classifica o widget no dump (itens têm apenas
        # x/y/w/h/text): o alvo do ui.type é o textarea com foco, então o
        # campo relevante do item é o texto visível, não um "name" inexistente.
        self.assertIsInstance(target.get("text"), str,
                              "item sem text para ui.type")

    def test_02_tres_help_enter_sem_travamento(self):
        """3 rounds help+Enter; a cada round, sys.info DEVE responder e a tela
        deve conter o header do help (evidência do Micro-Shell real)."""
        # Foco é pré-requisito do ui.type (ver focus_terminal_textarea):
        # sem o clique o texto não alcança o textarea e o teste mediria um
        # falso negativo (tela inalterada não é travamento). O Terminal é
        # aberto/confirmado aqui mesmo — sem herdar estado dos outros testes.
        self.ensure_terminal_active("teste 02")
        self.focus_terminal_textarea()
        for rnd in range(1, ROUNDS + 1):
            with self.subTest(round=rnd):
                self.record_finding("info", "help+Enter", f"round {rnd}: digitando help")
                self.type_help_enter(f"round {rnd}")

    def test_03_tela_mostra_help_do_micro_shell(self):
        """Executa SEU PRÓPRIO foco + help+Enter e só então verifica que o
        header do help do Micro-Shell aparece na tela. Não depende de ter
        rodado antes o teste 02 — a verificação só acontece depois deste
        teste digitar help por conta própria."""
        self.ensure_terminal_active("teste 03")
        self.focus_terminal_textarea()
        self.type_help_enter("teste 03 (help único)")
        joined = self.dump_texts("ui.dump pós help+Enter (teste 03)")
        self.assertIn(
            HELP_HEADER_HINT, joined,
            f"help do Micro-Shell não renderizado na tela; dump:\n{joined[:2000]}")

    def test_04_sucessivas_iteracoes_preservam_simbolos_do_shell(self):
        """Executa SEU PRÓPRIO foco + help+Enter e só então verifica que os
        símbolos do shell (ls/free/df/date/clear/help) estão presentes na
        tela. Não depende de estado deixado pelos testes anteriores."""
        self.ensure_terminal_active("teste 04")
        self.focus_terminal_textarea()
        self.type_help_enter("teste 04 (help único)")
        texts = self.dump_texts("ui.dump final (símbolos, teste 04)")
        for symbol in ("ls", "free", "df", "date", "clear", "help"):
            with self.subTest(symbol=symbol):
                self.assertIn(symbol, texts,
                              f"'{symbol}' ausente no help do Micro-Shell no device")


if __name__ == "__main__":
    unittest.main()
