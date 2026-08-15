#!/usr/bin/env python3
"""Focused tests for source-readiness command isolation and report drift checks."""

from __future__ import annotations

import importlib.util
import json
import pathlib
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
AUDITOR = ROOT / "Tools" / "SkyrimVR" / "audit_vr_readiness.py"


def load_auditor():
    spec = importlib.util.spec_from_file_location("stvr_audit_vr_readiness", AUDITOR)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


AUDIT = load_auditor()


class SourceReadinessTests(unittest.TestCase):
    def test_workflow_checks_the_pull_request_merge_revision(self) -> None:
        workflow = (ROOT / ".github/workflows/readiness.yml").read_text(encoding="utf-8")
        self.assertNotIn("pull_request.head.sha", workflow)
        self.assertIn("uses: actions/checkout@v4", workflow)

    def test_source_commands_keep_reports_out_of_checkout(self) -> None:
        commands = AUDIT.build_source_readiness_commands(pathlib.Path("/tmp/stvr-test-reports"))
        report_arguments = [argument for _, command in commands for argument in command]

        self.assertTrue(commands)
        self.assertTrue(all("Docs/SkyrimVR/" not in argument for argument in report_arguments))
        self.assertIn("--runtime-csv-dir", report_arguments)

    def test_committed_report_manifest_excludes_non_generated_prose(self) -> None:
        authoritative = {spec.path for spec in AUDIT.COMMITTED_REPORT_MANIFEST}

        self.assertNotIn(pathlib.Path("Docs/SkyrimVR/commonlib-layout-audit.md"), authoritative)
        self.assertIn(pathlib.Path("Docs/SkyrimVR/address-audit.json"), authoritative)
        self.assertIn(pathlib.Path("Docs/SkyrimVR/vr-services-audit.md"), authoritative)

    def test_report_comparators_detect_drift(self) -> None:
        with tempfile.TemporaryDirectory(prefix="stvr-readiness-test-") as temp:
            root = pathlib.Path(temp)
            committed = root / "committed.md"
            generated = root / "generated.md"
            committed.write_text("same\n", encoding="utf-8")
            generated.write_text("same\n", encoding="utf-8")

            self.assertEqual(AUDIT.compare_report_files(committed, generated, "bytes")[0], True)
            generated.write_text("different\n", encoding="utf-8")
            self.assertEqual(AUDIT.compare_report_files(committed, generated, "bytes")[0], False)

    def test_address_json_comparator_ignores_only_environment_paths(self) -> None:
        with tempfile.TemporaryDirectory(prefix="stvr-readiness-json-test-") as temp:
            root = pathlib.Path(temp)
            committed = root / "committed.json"
            generated = root / "generated.json"
            committed_payload = {
                "inputs": {
                    "refsRoot": "/checkout-a/_refs/skyrim_vr_address_library",
                    "releaseCsv": "/checkout-a/_refs/version-1-4-15-0.csv",
                    "runtimeCsvDir": "/checkout-a/GameFiles/Plugins",
                },
                "summary": {"missingNonRttiIds": 0},
            }
            generated_payload = json.loads(json.dumps(committed_payload))
            generated_payload["inputs"] = {
                "refsRoot": "/runner/_refs/skyrim_vr_address_library",
                "releaseCsv": "/runner/_refs/version-1-4-15-0.csv",
                "runtimeCsvDir": "/tmp/reports/plugins",
            }
            committed.write_text(json.dumps(committed_payload), encoding="utf-8")
            generated.write_text(json.dumps(generated_payload), encoding="utf-8")

            self.assertTrue(AUDIT.compare_report_files(committed, generated, "address-json")[0])
            generated_payload["summary"]["missingNonRttiIds"] = 1
            generated.write_text(json.dumps(generated_payload), encoding="utf-8")
            self.assertFalse(AUDIT.compare_report_files(committed, generated, "address-json")[0])

    def test_auditor_self_test_covers_report_drift(self) -> None:
        self.assertEqual(AUDIT.committed_report_consistency_self_test(), [])


if __name__ == "__main__":
    unittest.main()
