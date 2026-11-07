#!/usr/bin/env python3
"""WO06 source/compile/link checks. Does not start a node or access a wallet."""

import hashlib
import json
import pathlib
import re
import shlex
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
# Native Receive and activated payout integration are intentional changes. Keep
# their checkpoint pinned and continue checking private Qt capability isolation.
BASE = "a90f95beaeab9aff68e1cb20a2ee640c95366458"
MACRO = "MERCATURA_MERCAWEAVE_QT_RESEARCH_TEST_ONLY"


def main():
    build = pathlib.Path(sys.argv[1]).resolve(strict=True)
    git = lambda *args: subprocess.check_output(
        ["git", "-C", str(ROOT), *args], text=True
    )
    protected = git(
        "diff",
        BASE,
        "--name-only",
        "--",
        "src/mining",
        "src/crypto",
        "src/kernel/chainparams.cpp",
        "src/wallet/mining.cpp",
        "src/qt/miningsession.cpp",
        "src/qt/forms",
        "src/qt/qrimagewidget.cpp",
        "src/qt/guiutil.cpp",
        "src/psbt.cpp",
        "src/psbt.h",
    ).splitlines()
    # Exact reviewed WO08 secret-temporary cleanup; all other protected source
    # remains identical. This exception does not change the original UI base.
    hygiene = "src/crypto/hkdf_sha3_384.cpp"
    if hygiene in protected:
        if (ROOT / hygiene).read_text() != git(
            "show", "700929e22ef913a6868a7cbd1d379e72caf89c62:" + hygiene
        ):
            raise RuntimeError("HKDF change exceeds reviewed temporary cleanup")
        protected.remove(hygiene)
    if protected:
        raise RuntimeError("native mining/crypto/network/PSBT/QR path changed")
    erased = {}
    for file in ("receivecoinsdialog", "sendcoinsdialog", "miningpage"):
        path = "src/qt/" + file + ".cpp"
        source = (
            (ROOT / path).read_text().replace("#include <qt/mercaweavepanel.h>\n", "")
        )
        if file == "miningpage":
            source = source.replace(
                "    auto* grouped=new MercaWeaveQt::Panel(MercaWeaveQt::Panel::View::Mining,this);\n    grouped->setModel(wallet?wallet->getMercaWeaveModel():nullptr);\n    layout->addWidget(grouped);\n",
                "",
            )
        else:
            source = re.sub(
                r"    ui->verticalLayout->addWidget\(new MercaWeaveQt::Panel.*?\n",
                "",
                source,
            )
            source = source.replace(
                "    findChild<MercaWeaveQt::Panel*>()->setModel(_model?_model->getMercaWeaveModel():nullptr);\n",
                "",
            )
        if source != git("show", BASE + ":" + path):
            raise RuntimeError("original UI body changed: " + file)
        erased[path] = hashlib.sha256(source.encode()).hexdigest()
    definitions = git("grep", "-l", MACRO, "--", "*CMakeLists.txt").splitlines()
    if definitions != ["src/test/mercaweave/CMakeLists.txt"]:
        raise RuntimeError("mock capability macro exposed outside standalone project")
    allowed = {
        "mercaweave_durable_wallet",
        "mercaweave_durable_tests",
        "mercaweave_qt_isolated",
        "mercaweave_qt_model_tests",
        "mercaweave_reward_planner",
        "mercaweave_reward_tests",
        "mercaweave_qt_bridge",
        "mercaweave_qt_integration_tests",
    }
    commands = json.loads((build / "compile_commands.json").read_text())
    normal_command = None
    application = set()
    for entry in commands:
        command = entry["command"]
        if MACRO in command:
            target = re.search(r"CMakeFiles/([^/]+)\.dir/", command)
            if target is None or target[1] not in allowed:
                raise RuntimeError("mock capability compile definition leak")
        if "CMakeFiles/mercaweave_qt_normal_tests.dir/" in command:
            normal_command = shlex.split(command)
        if "CMakeFiles/mercaweave_qt_app_compile.dir/" in command:
            if "MERCATURA_MERCAWEAVE" in command:
                raise RuntimeError(
                    "normal application TU received research definitions"
                )
            application.add(pathlib.Path(entry["file"]).name)
    if (
        not {
            "receivecoinsdialog.cpp",
            "sendcoinsdialog.cpp",
            "walletmodel.cpp",
            "miningpage.cpp",
        }
        <= application
    ):
        raise RuntimeError("actual application compilation missing")
    for target in ("mercaweave_qt_normal_tests", "mercaweave_qt_integration_tests"):
        symbols = subprocess.check_output(["nm", "-C", str(build / target)], text=True)
        link = (build / f"CMakeFiles/{target}.dir/link.txt").read_text()
        if target.endswith("normal_tests"):
            if any(
                name in symbols
                for name in (
                    "attachIsolated",
                    "qt_research::Bridge::",
                    "ResearchEncryptPQSeed",
                    "reward_research::Planner::",
                )
            ):
                raise RuntimeError("production Qt exposes experimental backend")
            if "libmercaweave_qt_isolated" in link:
                raise RuntimeError("production Qt uses isolated ABI")
        elif "attachIsolated" not in symbols or "libmercaweave_qt_normal" in link:
            raise RuntimeError("isolated Qt backend/ABI check failed")
    if normal_command is None:
        raise RuntimeError("normal compiler command missing")
    args = []
    i = 0
    while i < len(normal_command):
        arg = normal_command[i]
        if arg == "-o":
            i += 2
            continue
        if arg == "-c":
            i += 1
            continue
        if arg.endswith("qt_model_tests.cpp"):
            i += 1
            continue
        args.append(arg)
        i += 1
    args += ["-fsyntax-only", "-x", "c++", "-"]
    result = subprocess.run(
        args,
        input="#include <test/mercaweave/qt_wallet_bridge.h>\n",
        text=True,
        capture_output=True,
    )
    if (
        result.returncode == 0
        or "Mock wallet capability injection is forbidden in production"
        not in result.stderr
    ):
        raise RuntimeError("normal headers admitted a mock wallet backend")
    print(
        json.dumps(
            {
                "passed": True,
                "normal_ui_source_erasure_sha256": erased,
                "native_mining_crypto_network_psbt_qr_unchanged": True,
                "production_mock_api_absent": True,
                "normal_application_compile_sources": sorted(application),
            },
            sort_keys=True,
        )
    )


if __name__ == "__main__":
    main()
