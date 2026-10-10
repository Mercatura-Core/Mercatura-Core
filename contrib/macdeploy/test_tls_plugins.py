#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Exercise actual plugin selection/path code without invoking Mach-O tools."""
import ast
import os
from pathlib import Path
import shutil
import tempfile
from types import SimpleNamespace
import unittest


class TLSDeploymentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = Path(__file__).with_name("macdeployqtplus").read_text()
        definitions = [node for node in ast.parse(source).body
                       if isinstance(node, (ast.ClassDef, ast.FunctionDef))
                       and node.name in ("DeploymentInfo", "deployPlugins")]
        cls.code = {"os": os, "shutil": shutil, "ApplicationBundleInfo": SimpleNamespace,
                    "getFrameworks": lambda *_: [], "runStrip": lambda *_: None}
        exec(compile(ast.Module(body=definitions, type_ignores=[]), "macdeployqtplus", "exec"), cls.code)

    def test_operational_tls_plugins_are_bundled(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            plugins, bundle = root / "plugins", root / "bundle"
            expected = ["platforms/libqcocoa.dylib", "styles/libqmacstyle.dylib",
                        "tls/libqopensslbackend.dylib", "tls/libqsecuretransportbackend.dylib"]
            for name in [*expected, "tls/unrelated.dylib", "imageformats/libqgif.dylib"]:
                path = plugins / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"public fixture")
            self.code["deployPlugins"](SimpleNamespace(pluginPath=str(bundle)),
                                       SimpleNamespace(pluginPath=str(plugins)), False, False)
            self.assertEqual(sorted(str(path.relative_to(bundle)) for path in bundle.rglob("*.dylib")), sorted(expected))

    def test_qt_install_layouts(self):
        with tempfile.TemporaryDirectory() as temporary:
            for relative in ("share/qt/plugins", "share/qt6/plugins", "plugins", "lib/qt6/plugins"):
                prefix = Path(temporary) / relative.replace("/", "-")
                plugins = prefix / relative
                plugins.mkdir(parents=True)
                deployment = self.code["DeploymentInfo"]()
                deployment.detectQtPath(str(prefix / "lib"))
                self.assertEqual(deployment.pluginPath, str(plugins))


if __name__ == "__main__":
    unittest.main()
