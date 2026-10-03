"""Recalcule toutes les formules d'un classeur Excel via LibreOffice (mode headless).

openpyxl n'evalue jamais les formules qu'il ecrit : tant que le fichier n'a pas ete
recalcule (ouvert et enregistre une fois dans Excel/LibreOffice, ou traite par ce
script), toute cellule-formule lue par pandas/openpyxl(data_only=True) renvoie None.
C'est pour ca que batch_compare.py recalcule le fichier apres y avoir injecte les
resultats de simulation.

Necessite LibreOffice installe sur la machine (executable 'soffice', parfois nomme
'libreoffice'). Sans lui, ce script ne peut rien faire : ouvre le fichier a la main
dans Excel ou LibreOffice Calc et enregistre-le une fois pour recalculer les formules.

Utilisation en ligne de commande :
    python recalc_xlsx.py coil_configs.xlsx [timeout_secondes]

Utilisation en import :
    from recalc_xlsx import recalc
    result = recalc("coil_configs.xlsx", timeout=60)
    if result.get("status") != "success":
        print(result.get("error", "recalcul incomplet"))
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

MACRO_FILENAME = "Module1.xba"

RECALCULATE_MACRO = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE script:module PUBLIC "-//OpenOffice.org//DTD OfficeDocument 1.0//EN" "module.dtd">
<script:module xmlns:script="http://openoffice.org/2000/script" script:name="Module1" script:language="StarBasic">
    Sub RecalculateAndSave()
      ThisComponent.calculateAll()
      ThisComponent.store()
      ThisComponent.close(True)
    End Sub
</script:module>"""

# Emplacements usuels quand 'soffice'/'libreoffice' n'est pas dans le PATH
FALLBACK_LOCATIONS = [
    "/Applications/LibreOffice.app/Contents/MacOS/soffice",           # macOS
    r"C:\Program Files\LibreOffice\program\soffice.exe",              # Windows
    r"C:\Program Files (x86)\LibreOffice\program\soffice.exe",        # Windows 32-bit
]


def _find_soffice():
    for name in ("soffice", "libreoffice"):
        path = shutil.which(name)
        if path:
            return path
    for candidate in FALLBACK_LOCATIONS:
        if os.path.exists(candidate):
            return candidate
    return None


def _stamp(path):
    st = os.stat(path)
    return st.st_mtime_ns, st.st_size


def recalc(path, timeout=60):
    """Recalcule le classeur en place. Retourne un dict avec soit
    {"status": "success"} soit {"error": "..."} expliquant l'echec."""

    if not Path(path).exists():
        return {"error": f"Fichier introuvable : {path}"}

    soffice = _find_soffice()
    if soffice is None:
        return {
            "error": (
                "LibreOffice ('soffice') introuvable dans le PATH. Installe LibreOffice "
                "(https://www.libreoffice.org/download/), ou ouvre le fichier dans Excel "
                "/ LibreOffice Calc et enregistre-le une fois pour recalculer les formules "
                "manuellement -- ce n'est pas bloquant, juste moins pratique."
            )
        }

    abs_path = str(Path(path).absolute())

    with tempfile.TemporaryDirectory(prefix="recalc-lo-profile-") as profile_dir_str:
        profile_dir = Path(profile_dir_str)
        profile_url = profile_dir.as_uri()

        started = time.monotonic()
        try:
            subprocess.run(
                [soffice, "--headless", "--terminate_after_init", f"-env:UserInstallation={profile_url}"],
                capture_output=True, timeout=timeout,
            )
        except subprocess.TimeoutExpired:
            return {"error": "LibreOffice n'a pas repondu lors de la creation de son profil."}
        except FileNotFoundError:
            return {"error": "LibreOffice ('soffice') introuvable au chemin detecte."}

        macro_dir = profile_dir / "user" / "basic" / "Standard"
        if not macro_dir.exists():
            return {"error": "LibreOffice n'a pas cree de profil utilisable ; formules NON recalculees."}
        try:
            (macro_dir / MACRO_FILENAME).write_text(RECALCULATE_MACRO)
        except OSError as e:
            return {"error": f"Impossible d'installer la macro de recalcul : {e}"}

        remaining = max(5, int(timeout - (time.monotonic() - started)))
        before = _stamp(abs_path)

        cmd = [
            soffice, "--headless", "--norestore", f"-env:UserInstallation={profile_url}",
            "vnd.sun.star.script:Standard.Module1.RecalculateAndSave?language=Basic&location=application",
            abs_path,
        ]
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=remaining + 15)
        except subprocess.TimeoutExpired:
            return {"error": f"LibreOffice a depasse le delai de {timeout}s ; relance avec un timeout plus long."}
        except FileNotFoundError:
            return {"error": "LibreOffice ('soffice') introuvable au chemin detecte."}

        if result.returncode != 0:
            detail = (result.stderr or "").strip() or f"soffice a quitte avec le code {result.returncode}"
            return {"error": f"Echec du recalcul LibreOffice : {detail}"}

        if _stamp(abs_path) == before:
            return {
                "error": (
                    "LibreOffice s'est termine normalement mais n'a pas reecrit le fichier : "
                    "rien n'a ete recalcule. Verifie qu'aucune autre instance de LibreOffice "
                    "n'est ouverte, puis reessaie."
                )
            }

    return {"status": "success"}


def main():
    if len(sys.argv) < 2:
        print("Usage : python recalc_xlsx.py <fichier.xlsx> [timeout_secondes]")
        sys.exit(1)
    path = sys.argv[1]
    timeout = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    result = recalc(path, timeout=timeout)
    print(json.dumps(result, indent=2, ensure_ascii=False))
    sys.exit(0 if result.get("status") == "success" else 1)


if __name__ == "__main__":
    main()
