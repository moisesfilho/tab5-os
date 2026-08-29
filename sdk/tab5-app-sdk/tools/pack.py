#!/usr/bin/env python3
"""
pack.py - Tab5 OS Application Packaging Tool
Valida o manifest.json e empacota uma aplicação em formato .tab5pkg
"""

import argparse
import json
import os
import re
import shutil
import sys

VALID_ID_REGEX = re.compile(r'^[a-z0-9_\.\-]+$')
VALID_SEMVER_REGEX = re.compile(r'^\d+\.\d+\.\d+(-[a-zA-Z0-9.]+)?$')

def validate_manifest(manifest_path):
    if not os.path.exists(manifest_path):
        raise FileNotFoundError(f"Arquivo de manifesto nao encontrado: {manifest_path}")

    with open(manifest_path, 'r', encoding='utf-8') as f:
        try:
            data = json.load(f)
        except json.JSONDecodeError as e:
            raise ValueError(f"JSON invalido em {manifest_path}: {e}")

    # Campos obrigatorios
    for field in ['id', 'name', 'version']:
        if field not in data or not str(data[field]).strip():
            raise ValueError(f"Campo obrigatorio ausente ou vazio no manifesto: '{field}'")

    app_id = str(data['id']).strip()
    if not VALID_ID_REGEX.match(app_id):
        raise ValueError(f"ID de aplicacao invalido '{app_id}'. Use apenas letras minusculas, numeros, pontos, hifens e underlines.")

    version = str(data['version']).strip()
    if not VALID_SEMVER_REGEX.match(version):
        raise ValueError(f"Versao '{version}' nao segue o padrao SemVer (ex: 1.0.0)")

    entry = data.get('entry', 'app.wasm')
    return data, entry

def pack_app(app_dir, output_dir=None, package_name=None):
    app_dir = os.path.abspath(app_dir)
    manifest_file = os.path.join(app_dir, "manifest.json")
    manifest_data, entry_name = validate_manifest(manifest_file)

    app_id = manifest_data['id']
    entry_file = os.path.join(app_dir, entry_name)

    if not os.path.exists(entry_file):
        raise FileNotFoundError(f"Binario de entrada '{entry_name}' nao encontrado em {app_dir}")

    if not package_name:
        package_name = f"{app_id}.tab5pkg"
    elif not package_name.endswith(".tab5pkg"):
        package_name += ".tab5pkg"

    if not output_dir:
        output_dir = os.path.join(app_dir, "dist")
    os.makedirs(output_dir, exist_ok=True)

    dest_pkg_dir = os.path.join(output_dir, package_name)
    if os.path.exists(dest_pkg_dir):
        shutil.rmtree(dest_pkg_dir)
    os.makedirs(dest_pkg_dir, exist_ok=True)

    # Copia manifest.json
    shutil.copy2(manifest_file, os.path.join(dest_pkg_dir, "manifest.json"))

    # Copia binario de entrada
    shutil.copy2(entry_file, os.path.join(dest_pkg_dir, entry_name))

    # Copia pasta assets se existir
    assets_dir = os.path.join(app_dir, "assets")
    if os.path.isdir(assets_dir):
        shutil.copytree(assets_dir, os.path.join(dest_pkg_dir, "assets"))

    print(f"[OK] Pacote Tab5 criado com sucesso: {dest_pkg_dir}")
    print(f"     App ID: {manifest_data['id']}")
    print(f"     Nome: {manifest_data['name']}")
    print(f"     Versao: {manifest_data['version']}")
    return dest_pkg_dir

def main():
    parser = argparse.ArgumentParser(description="Tab5 OS Package Builder")
    parser.add_argument("app_dir", help="Diretório da aplicação contendo manifest.json")
    parser.add_argument("-o", "--output", help="Diretório de saída do pacote", default=None)
    parser.add_argument("-n", "--name", help="Nome customizado para o pacote .tab5pkg", default=None)

    args = parser.parse_args()
    try:
        pack_app(args.app_dir, args.output, args.name)
        sys.exit(0)
    except Exception as e:
        print(f"[ERRO] {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
