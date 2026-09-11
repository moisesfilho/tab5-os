#!/usr/bin/env python3
"""Contrato estático (host) da seleção de transporte do serial_bridge.

Extensão do plano "Boneco de Lata" (boneco-de-lata.md): além da UART física
dedicada, o bridge de automação passa a poder usar o console USB-Serial-JTAG
do firmware — o mesmo console por onde saem os logs ESP_LOG. Esta suíte fixa o
contrato de ``components/os/Kconfig`` ANTES da implementação (TDD vermelho):
hoje o Kconfig só conhece a UART física; a escolha de transporte
UART/USB-Serial-JTAG ainda não existe, então os testes de seleção falham até o
firmware implementá-la.

O que fica vinculante (o developer implementa contra isto; divergir exige
justificar a mudança do teste):

1. ``TAB5_SERIAL_BRIDGE_ENABLED`` continua um símbolo ``config`` (o contrato
   existente não é enfraquecido).
2. Existe uma ``choice`` nomeada ``TAB5_SERIAL_BRIDGE_TRANSPORT`` com exatamente
   os dois membros:
     - ``TAB5_SERIAL_BRIDGE_TRANSPORT_UART``            (UART física dedicada)
     - ``TAB5_SERIAL_BRIDGE_TRANSPORT_USB_SERIAL_JTAG`` (console compartilhado,
       logs podem intercalar com NDJSON)
   A choice materializa o "OU exclusivo" UART vs USB-Serial-JTAG da seleção.
3. A choice tem ``default`` determinístico apontando para um dos dois membros
   (build reproduzível sem interação de menuconfig).
4. As configurações específicas da UART (``UART_NUM``/``BAUD``/``RX_PIN``/
   ``TX_PIN``) exigem ``depends on TAB5_SERIAL_BRIDGE_TRANSPORT_UART``: no modo
   USB-Serial-JTAG elas não podem valer nem ofuscar a configuração do console.
5. Nenhum símbolo ``config`` é redefinido no arquivo (o Kconfig do ESP-IDF
   rejeita redefinições; duplicidade quebra o build).
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KCONFIG = ROOT / "components/os/Kconfig"
HAS_KCONFIG = KCONFIG.is_file()

CHOICE_NAME = "TAB5_SERIAL_BRIDGE_TRANSPORT"
TRANSPORT_UART = "TAB5_SERIAL_BRIDGE_TRANSPORT_UART"
TRANSPORT_USB_JTAG = "TAB5_SERIAL_BRIDGE_TRANSPORT_USB_SERIAL_JTAG"
UART_DEPENDENT_SYMBOLS = (
    "TAB5_SERIAL_BRIDGE_UART_NUM",
    "TAB5_SERIAL_BRIDGE_UART_BAUD",
    "TAB5_SERIAL_BRIDGE_UART_RX_PIN",
    "TAB5_SERIAL_BRIDGE_UART_TX_PIN",
)

KCONFIG_MISSING_REASON = (
    "FALHA ESPERADA (pré-implementação): components/os/Kconfig ainda não existe. "
    "Quando o módulo serial_bridge ganhar o Kconfig com a seleção de transporte "
    "UART/USB-Serial-JTAG, esta suíte roda de verdade."
)


def parse_kconfig():
    """Varre components/os/Kconfig.

    Devolve ``(configs, choices, defined_symbols)``:
    - ``configs``: dict nome -> {"depends": [símbolos...]}
    - ``choices``: lista de dicts {"name", "members": [símbolos...], "default"}
    - ``defined_symbols``: nomes de ``config`` na ordem de aparição (para
      detectar redefinições)
    """
    configs = {}
    choices = []
    defined_symbols = []
    current_choice = None
    current_config = None

    for line in KCONFIG.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue

        m = re.match(r"config[ \t]+([A-Z0-9_]+)$", stripped)
        if m:
            current_config = m.group(1)
            configs.setdefault(current_config, {"depends": []})
            defined_symbols.append(current_config)
            if current_choice is not None:
                current_choice["members"].append(current_config)
            continue

        m = re.match(r"choice[ \t]*([A-Z0-9_]*)$", stripped)
        if m:
            current_choice = {
                "name": m.group(1) or None,
                "members": [],
                "default": None,
            }
            choices.append(current_choice)
            continue

        if re.match(r"endchoice[ \t]*$", stripped):
            current_choice = None
            continue

        m = re.match(r"default[ \t]+([A-Z0-9_]+)$", stripped)
        if m and current_choice is not None:
            current_choice["default"] = m.group(1)
            continue

        m = re.match(r"depends on[ \t]+([A-Z0-9_]+)", stripped)
        if m and current_config is not None:
            dep = m.group(1)
            if dep not in configs[current_config]["depends"]:
                configs[current_config]["depends"].append(dep)
            continue

        # bool/int/string/hex/range/prompt/help: irrelevantes para o contrato
        # aqui fixado; seguem sem afetar a varredura.

    return configs, choices, defined_symbols


@unittest.skipUnless(HAS_KCONFIG, KCONFIG_MISSING_REASON)
class SerialBridgeKconfigTransportContract(unittest.TestCase):
    """Seleção de transporte UART/USB-Serial-JTAG no Kconfig do bridge."""

    @classmethod
    def setUpClass(cls):
        cls.configs, cls.choices, cls.defined_symbols = parse_kconfig()

    @classmethod
    def _choice(cls, name):
        for choice in cls.choices:
            if choice["name"] == name:
                return choice
        return None

    def test_kconfig_file_exists(self):
        self.assertTrue(KCONFIG.is_file(), f"Kconfig ausente: {KCONFIG}")

    def test_legacy_enabled_symbol_is_preserved(self):
        self.assertIn(
            "TAB5_SERIAL_BRIDGE_ENABLED", self.configs,
            "TAB5_SERIAL_BRIDGE_ENABLED (contrato existente) precisa continuar "
            "no Kconfig")

    def test_named_transport_choice_exists(self):
        self.assertIsNotNone(
            self._choice(CHOICE_NAME),
            "falta a choice nomeada TAB5_SERIAL_BRIDGE_TRANSPORT — seleção "
            "UART/USB-Serial-JTAG do bridge ainda não existe")

    def test_choice_has_both_transport_options(self):
        choice = self._choice(CHOICE_NAME)
        self.assertIsNotNone(choice)
        self.assertEqual(
            set(choice["members"]),
            {TRANSPORT_UART, TRANSPORT_USB_JTAG},
            "a choice precisa ter exatamente as duas opções: UART física "
            "dedicada (TAB5_SERIAL_BRIDGE_TRANSPORT_UART) e USB-Serial-JTAG "
            "compartilhado com o console (TAB5_SERIAL_BRIDGE_TRANSPORT_"
            "USB_SERIAL_JTAG)")

    def test_choice_has_deterministic_default(self):
        choice = self._choice(CHOICE_NAME)
        self.assertIsNotNone(choice)
        self.assertIn(
            choice["default"],
            {TRANSPORT_UART, TRANSPORT_USB_JTAG},
            "a choice precisa ter um default apontando para UMA das duas "
            "opções de transporte (build determinístico sem menuconfig)")

    def test_uart_dependents_require_uart_transport_selected(self):
        for symbol in UART_DEPENDENT_SYMBOLS:
            self.assertIn(
                symbol, self.configs,
                f"falta {symbol} no Kconfig do bridge")
            self.assertIn(
                TRANSPORT_UART, self.configs[symbol]["depends"],
                f"{symbol} precisa exigir 'depends on {TRANSPORT_UART}' — no "
                "modo USB-Serial-JTAG as definições de UART dedicada não podem "
                "valer nem esconder a configuração do console")

    def test_no_duplicate_config_symbols(self):
        self.assertEqual(
            len(self.defined_symbols), len(set(self.defined_symbols)),
            "Kconfig rejeita redefinição de símbolo config; duplicidade "
            "quebraria a configuração do bridge")


if __name__ == "__main__":
    unittest.main(verbosity=2)
