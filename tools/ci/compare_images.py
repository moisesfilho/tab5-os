#!/usr/bin/env python3
"""Compara capturas do simulador (out/) contra os goldens de referencia.

Uso:
  compare_images.py [--goldens DIR] [--out DIR] [--tolerance PCT]
                    [--rgb-delta N] [--diff-dir DIR] [--scenario NOME]
                    [--report PATH]

Sem argumentos, compara tests/simulator/goldens/<cenario>/01_*.bmp contra
tests/simulator/out/<cenario>/01_*.bmp.

Criterio de PASS:
  - Mesmo nome de arquivo presente nos dois lados.
  - Mesmas dimensoes.
  - Proporcao de pixels diferentes (delta RGB > --rgb-delta) menor que
    --tolerance (padrao 0.5% da area).

Quando falha, grava em --diff-dir um PNG com as regioes diferentes
destacadas (vermelho), alem de um relatorio texto em --report.
"""

import argparse
import os
import sys

try:
    from PIL import Image
except ImportError:
    print("erro: Pillow nao instalado (pip install Pillow)", file=sys.stderr)
    sys.exit(1)


def load_rgb(path):
    with Image.open(path) as im:
        return im.convert("RGB")


def diff_images(golden, out, rgb_delta):
    """Retorna (w, h, mask) onde mask[e] = True se o pixel difere."""
    if golden.size != out.size:
        return None, None, None
    w, h = golden.size
    g = list(golden.get_flattened_data())
    o = list(out.get_flattened_data())
    mask = [False] * (w * h)
    for i in range(w * h):
        gp = g[i]
        op = o[i]
        if abs(gp[0] - op[0]) > rgb_delta or abs(gp[1] - op[1]) > rgb_delta or abs(gp[2] - op[2]) > rgb_delta:
            mask[i] = True
    return w, h, mask


def write_diff_png(golden, out, mask, path):
    base = golden.copy()
    px = base.load()
    w, h = base.size
    for i in range(w * h):
        if mask[i]:
            px[i % w, i // w] = (255, 0, 0)
    base.save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--goldens", default="tests/simulator/goldens")
    parser.add_argument("--out", default="tests/simulator/out")
    parser.add_argument("--tolerance", type=float, default=0.5,
                        help="percentual maximo de pixels divergentes (padrao 0.5)")
    parser.add_argument("--rgb-delta", type=int, default=48,
                        help="delta RGB por canal que define um pixel divergente (padrao 48)")
    parser.add_argument("--diff-dir", default="tests/simulator/diffs",
                        help="onde gravar PNGs de diferenca quando falhar")
    parser.add_argument("--scenario", help="compara apenas este cenario")
    parser.add_argument("--report", default="tests/simulator/report.txt")
    args = parser.parse_args()

    golden_root = os.path.abspath(args.goldens)
    out_root = os.path.abspath(args.out)

    scenarios = sorted(os.listdir(golden_root))
    if args.scenario:
        if args.scenario not in scenarios:
            print(f"erro: cenario '{args.scenario}' nao existe em {golden_root}", file=sys.stderr)
            sys.exit(1)
        scenarios = [args.scenario]

    passed = 0
    failed = 0
    total_pairs = 0
    report_lines = []

    for scen in scenarios:
        gdir = os.path.join(golden_root, scen)
        odir = os.path.join(out_root, scen)
        goldens = sorted(f for f in os.listdir(gdir) if f.endswith(".bmp"))
        outs = sorted(f for f in os.listdir(odir) if f.endswith(".bmp")) if os.path.isdir(odir) else []
        report_lines.append(f"[{scen}]")
        if goldens != outs:
            missing = set(goldens) ^ set(outs)
            report_lines.append(f"  FALHA: arquivos divergentes: {sorted(missing)}")
            failed += 1
            continue

        for name in goldens:
            total_pairs += 1
            gpath = os.path.join(gdir, name)
            opath = os.path.join(odir, name)
            golden = load_rgb(gpath)
            out = load_rgb(opath)
            if golden.size != out.size:
                report_lines.append(f"  FALHA: {name} dimensoes {golden.size} != {out.size}")
                failed += 1
                continue
            w, h, mask = diff_images(golden, out, args.rgb_delta)
            total_px = w * h
            diverging = sum(mask)
            ratio = (diverging / total_px) * 100.0
            if ratio <= args.tolerance:
                report_lines.append(f"  PASS:  {name} (diff {ratio:.3f}%)")
                passed += 1
            else:
                report_lines.append(f"  FALHA: {name} (diff {ratio:.3f}% > {args.tolerance}%)")
                os.makedirs(os.path.join(args.diff_dir, scen), exist_ok=True)
                write_diff_png(golden, out, mask, os.path.join(args.diff_dir, scen, name.replace(".bmp", ".png")))
                failed += 1

    summary = f"\nResultado: {passed} PASS, {failed} FALHA (pares comparados: {total_pairs})"
    report_lines.append(summary)
    report = "\n".join(report_lines)
    print(report)
    os.makedirs(os.path.dirname(os.path.abspath(args.report)) or ".", exist_ok=True)
    with open(args.report, "w", encoding="utf-8") as f:
        f.write(report + "\n")
    print(f"\nRelatorio: {os.path.abspath(args.report)}")

    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
