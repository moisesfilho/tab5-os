#!/usr/bin/env python3
"""Contrato de linkagem do simulador host (tests/simulator).

O executável ``tab5_sim`` compila o runtime WASM real
(``components/os/runtime/*.cpp``) e o shell real sobre LVGL+SDL2. Em
``46a3417`` o runtime passou a consumir o despachador assíncrono
``tab5_wasm_dispatcher.*``, mas o ``CMakeLists.txt`` do simulador não foi
atualizado — o build falha em ``collect2`` com referências indefinidas para
``tab5_wasm_dispatch_post_*`` (erro reproduzido em set/2026):

    CMakeFiles/tab5_sim.dir/.../tab5_package_mgr.cpp.o: referência não
        definida para "tab5_wasm_dispatch_post_launch"
    CMakeFiles/tab5_sim.dir/.../tab5_ui_host.cpp.o: referência não definida
        para "tab5_wasm_dispatch_post_call"
    CMakeFiles/tab5_sim.dir/.../tab5_lifecycle_host.cpp.o: referência não
        definida para "tab5_wasm_dispatch_post_string"

Critério de aceite: o CMake do simulador compila o despachador real (sem
mock), resgatando a linkagem — e, uma vez linkado, o comparador visual roda
contra os goldens sem novas falhas de símbolo.

Fase vermelha (TDD): hoje ``tab5_wasm_dispatcher.cpp`` está fora da lista de
fontes do ``add_executable(tab5_sim ...)``; os testes deste arquivo que
verificam a presença/paridade do despachador falham até o CMake do sim ser
corrigido. Os demais (definição de símbolos, ausência de mock) já valem.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SIM_CMAKE = (ROOT / "tests/simulator/CMakeLists.txt").read_text(encoding="utf-8")
HOST_CMAKE = (ROOT / "tests/host/CMakeLists.txt").read_text(encoding="utf-8")
DISPATCHER_SRC = (ROOT / "components/os/runtime/tab5_wasm_dispatcher.cpp").read_text(
    encoding="utf-8"
)
DISPATCHER_HDR = (ROOT / "components/os/runtime/tab5_wasm_dispatcher.h").read_text(
    encoding="utf-8"
)

# Símbolos consumidos pelo runtime e fornecidos pelo despachador.
DISPATCH_SYMBOLS = (
    "tab5_wasm_dispatch_post_call",
    "tab5_wasm_dispatch_post_string",
    "tab5_wasm_dispatch_post_launch",
)

# Consumidores já compilados pelo sim (prova de que o link só depende do
# despachador ser adicionado ao CMake).
CONSUMERS_IN_SIM = {
    "tab5_package_mgr.cpp": ("tab5_wasm_dispatch_post_launch",),
    "tab5_lifecycle_host.cpp": ("tab5_wasm_dispatch_post_string",),
    "tab5_ui_host.cpp": ("tab5_wasm_dispatch_post_call",),
}

OSE = "${OS}/runtime/"


def sim_runtime_sources() -> set[str]:
    """Fonte .cpp de os/runtime listadas no add_executable(tab5_sim ...)."""
    return set(re.findall(rf"{re.escape(OSE)}([A-Za-z0-9_]+\.cpp)", SIM_CMAKE))


def host_runtime_sources() -> set[str]:
    """Fonte .cpp de os/runtime listadas no tab5_host_tests."""
    return set(
        re.findall(
            rf"\${{REPO_ROOT}}/components/os/runtime/([A-Za-z0-9_]+\.cpp)",
            HOST_CMAKE,
        )
    )


class SimLinksWasDispatcherContract(unittest.TestCase):
    """O simulador deve compilar o despachador WASM real (critério de aceite)."""

    def test_sim_cmake_lists_wasm_dispatcher(self):
        self.assertIn(
            f"{OSE}tab5_wasm_dispatcher.cpp",
            SIM_CMAKE,
            "tests/simulator/CMakeLists.txt não lista "
            "${OS}/runtime/tab5_wasm_dispatcher.cpp no add_executable "
            "(tab5_sim ...); sem ele, a linkagem falha em collect2 com "
            "tab5_wasm_dispatch_post_* indefinido",
        )

    def test_sim_runtime_sources_match_host_parity(self):
        missing = host_runtime_sources() - sim_runtime_sources()
        self.assertEqual(
            missing,
            set(),
            "runtime/ módulos compilados pelos testes host faltam no "
            f"simulador (mesma classe da falha do despachador): {sorted(missing)}",
        )


class DispatcherSymbolsContract(unittest.TestCase):
    """Os símbolos chamados pelo runtime existem, no módulo real e sem mock."""

    def test_dispatcher_defines_all_consumed_symbols(self):
        for symbol in DISPATCH_SYMBOLS:
            self.assertRegex(
                DISPATCHER_SRC,
                rf'extern\s+"C"\s+[\w:]+\s+{symbol}\s*\(',
                f"{symbol} não está definido (extern \"C\" + tipo + nome) em "
                "tab5_wasm_dispatcher.cpp",
            )

    def test_dispatcher_header_declares_all_symbols(self):
        for symbol in DISPATCH_SYMBOLS:
            self.assertIn(
                symbol,
                DISPATCHER_HDR,
                f"{symbol} não está declarado em tab5_wasm_dispatcher.h",
            )

    def test_no_shim_stubs_dispatch_symbols(self):
        shims_dir = ROOT / "tests/simulator/shims"
        for path in shims_dir.rglob("*"):
            if not path.is_file() or path.suffix not in (".c", ".cpp", ".h", ".hpp"):
                continue
            hit = [s for s in DISPATCH_SYMBOLS if s in path.read_text(encoding="utf-8")]
            self.assertEqual(
                hit,
                [],
                f"{path.relative_to(ROOT)} define/referencia {hit}; o simulador "
                "deve linkar o despachador real de os/runtime, não um mock",
            )


class SimLinkConsumersContract(unittest.TestCase):
    """Os consumidores do despachador já fazem parte do link do simulador."""

    def test_every_consumer_compiles_in_sim(self):
        for source, symbols in CONSUMERS_IN_SIM.items():
            self.assertIn(
                f"{OSE}{source}",
                SIM_CMAKE,
                f"{source} (consumidor do despachador) não está no CMake do sim",
            )

    def test_consumers_refer_to_dispatch_symbols(self):
        for source, symbols in CONSUMERS_IN_SIM.items():
            body = (ROOT / "components/os/runtime" / source).read_text(
                encoding="utf-8"
            )
            for symbol in symbols:
                self.assertIn(
                    symbol,
                    body,
                    f"{source} não referencia {symbol} (contrato desatualizado)",
                )


if __name__ == "__main__":
    unittest.main(verbosity=2)
