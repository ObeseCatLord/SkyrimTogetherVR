#!/usr/bin/env python3
"""Focused manifest-contract tests for an explicitly packaged patched PLANCK DLL."""

from __future__ import annotations

import importlib.util
import pathlib
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
TOOLS = ROOT / "Tools" / "SkyrimVR"
SCRIPT = TOOLS / "audit_built_package.py"
sys.path.insert(0, str(TOOLS))


def load_audit_module():
    spec = importlib.util.spec_from_file_location("stvr_audit_built_package", SCRIPT)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


AUDIT = load_audit_module()
import local_handoff_artifacts


class PatchedPlanckPackageContractTests(unittest.TestCase):
    @staticmethod
    def dependency_provenance(artifact_hash: str) -> dict[str, object]:
        return {
            "schema": AUDIT.PATCHED_PLANCK_PROVENANCE_SCHEMA,
            "artifactName": AUDIT.PATCHED_PLANCK_ARTIFACT_NAME,
            "artifactSha256": artifact_hash,
            "planckCommit": "1" * 40,
            "planckSourceTreeSha256": "2" * 64,
            "havokArchiveSha256": "3" * 64,
            "havokCompatibilityPatch": AUDIT.PATCHED_PLANCK_HAVOK_COMPATIBILITY_PATCH,
            "havokSourceTreeSha256": "4" * 64,
            "havokSourceFileCount": 101,
            "sksevrArchiveSha256": "5" * 64,
            "sksevrSourceTreeSha256": "6" * 64,
            "sksevrSourceFileCount": 403,
            "msbuildTarget": "Rebuild",
        }

    @classmethod
    def patched_planck_marker(cls, artifact_hash: str) -> dict[str, object]:
        provenance = cls.dependency_provenance(artifact_hash)
        return {
            "name": AUDIT.PATCHED_PLANCK_ARTIFACT_NAME,
            "packagePath": AUDIT.PATCHED_PLANCK_PACKAGE_PATH,
            "sha256": artifact_hash,
            "interface": AUDIT.PATCHED_PLANCK_INTERFACE,
            "planckCommit": provenance["planckCommit"],
            "planckSourceTreeSha256": provenance["planckSourceTreeSha256"],
            "havokArchiveSha256": provenance["havokArchiveSha256"],
            "havokCompatibilityPatch": provenance["havokCompatibilityPatch"],
            "havokSourceTreeSha256": provenance["havokSourceTreeSha256"],
            "havokSourceFileCount": provenance["havokSourceFileCount"],
            "sksevrArchiveSha256": provenance["sksevrArchiveSha256"],
            "sksevrSourceTreeSha256": provenance["sksevrSourceTreeSha256"],
            "sksevrSourceFileCount": provenance["sksevrSourceFileCount"],
            "forcedBuildArtifactSha256": artifact_hash,
            "forcedBuildTarget": "Rebuild",
        }

    def test_interface002_marker_binds_the_packaged_dll_and_hashes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            package = pathlib.Path(temporary)
            artifact = package / AUDIT.PATCHED_PLANCK_PACKAGE_PATH
            AUDIT.write_x64_pe(artifact)
            artifact_hash = AUDIT.sha256(artifact)
            manifest = {
                "patchedPlanckArtifact": self.patched_planck_marker(artifact_hash),
                "patchedPlanckProvenance": self.dependency_provenance(artifact_hash),
                "copiedArtifacts": [AUDIT.PATCHED_PLANCK_ARTIFACT_NAME],
                "expectedArtifacts": [AUDIT.PATCHED_PLANCK_ARTIFACT_NAME],
                "artifactSha256": {AUDIT.PATCHED_PLANCK_ARTIFACT_NAME: artifact_hash},
                "packageFileSha256": {AUDIT.PATCHED_PLANCK_PACKAGE_PATH: artifact_hash},
            }

            failures = []
            AUDIT.audit_patched_planck_artifact(package, manifest, failures, require_interface002=True)
            self.assertEqual(failures, [])

            manifest["patchedPlanckArtifact"]["interface"] = "interface001"
            failures = []
            AUDIT.audit_patched_planck_artifact(package, manifest, failures, require_interface002=True)
            self.assertIn("build manifest patched PLANCK interface marker is not interface002: 'interface001'", failures)

            manifest["patchedPlanckArtifact"]["interface"] = AUDIT.PATCHED_PLANCK_INTERFACE
            artifact.write_bytes(artifact.read_bytes() + b"changed")
            failures = []
            AUDIT.audit_patched_planck_artifact(package, manifest, failures, require_interface002=True)
            self.assertIn("build manifest patched PLANCK SHA-256 mismatch", failures)

    def test_forced_rebuild_provenance_cannot_be_detached_from_package_dll(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            package = pathlib.Path(temporary)
            artifact = package / AUDIT.PATCHED_PLANCK_PACKAGE_PATH
            AUDIT.write_x64_pe(artifact)
            artifact_hash = AUDIT.sha256(artifact)
            manifest = {
                "patchedPlanckArtifact": self.patched_planck_marker(artifact_hash),
                "patchedPlanckProvenance": self.dependency_provenance("c" * 64),
                "copiedArtifacts": ["activeragdoll.dll"],
                "expectedArtifacts": ["activeragdoll.dll"],
                "artifactSha256": {"activeragdoll.dll": artifact_hash},
                "packageFileSha256": {AUDIT.PATCHED_PLANCK_PACKAGE_PATH: artifact_hash},
            }
            manifest["patchedPlanckArtifact"]["forcedBuildArtifactSha256"] = "c" * 64
            manifest["patchedPlanckArtifact"]["forcedBuildTarget"] = "Build"
            failures = []
            AUDIT.audit_patched_planck_artifact(package, manifest, failures, require_interface002=True)
            self.assertIn(
                "build manifest forced PLANCK artifact SHA-256 does not bind the packaged DLL", failures
            )
            self.assertIn("build manifest patched PLANCK was not produced by MSBuild Rebuild", failures)

    def test_dependency_provenance_must_be_complete_and_bound_to_the_package_marker(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            package = pathlib.Path(temporary)
            artifact = package / AUDIT.PATCHED_PLANCK_PACKAGE_PATH
            AUDIT.write_x64_pe(artifact)
            artifact_hash = AUDIT.sha256(artifact)
            manifest = {
                "patchedPlanckArtifact": self.patched_planck_marker(artifact_hash),
                "patchedPlanckProvenance": self.dependency_provenance(artifact_hash),
                "copiedArtifacts": [AUDIT.PATCHED_PLANCK_ARTIFACT_NAME],
                "expectedArtifacts": [AUDIT.PATCHED_PLANCK_ARTIFACT_NAME],
                "artifactSha256": {AUDIT.PATCHED_PLANCK_ARTIFACT_NAME: artifact_hash},
                "packageFileSha256": {AUDIT.PATCHED_PLANCK_PACKAGE_PATH: artifact_hash},
            }

            manifest["patchedPlanckArtifact"].pop("havokSourceTreeSha256")
            manifest["patchedPlanckProvenance"].pop("havokCompatibilityPatch")
            manifest["patchedPlanckProvenance"]["sksevrSourceFileCount"] = 0
            manifest["patchedPlanckProvenance"]["havokArchiveSha256"] = "7" * 64
            failures = []
            AUDIT.audit_patched_planck_artifact(package, manifest, failures, require_interface002=True)
            self.assertIn(
                "build manifest patched PLANCK dependency field havokSourceTreeSha256 is missing or invalid",
                failures,
            )
            self.assertIn(
                "build manifest patched PLANCK provenance dependency field sksevrSourceFileCount is missing or invalid",
                failures,
            )
            self.assertIn(
                "build manifest patched PLANCK provenance dependency field havokCompatibilityPatch is missing or invalid",
                failures,
            )
            self.assertIn(
                "build manifest patched PLANCK havokArchiveSha256 does not bind forced-build provenance",
                failures,
            )

    def test_candidate_script_declares_synthetic_planck_and_root_snapshot_contract(self) -> None:
        candidate = (TOOLS / "build_winboat_candidate.sh").read_text(encoding="utf-8")
        for required in (
            "planck_synthetic_commit",
            "root_synthetic_commit",
            "PLANCK synthetic bundle",
            "root synthetic snapshot",
        ):
            self.assertIn(required, candidate)

    def test_both_winboat_builds_use_complete_planck_wrapper_and_pinned_provisioner(self) -> None:
        for script_name in ("build_winboat_candidate.sh", "build_winboat_gameplay.sh"):
            script = (TOOLS / script_name).read_text(encoding="utf-8")
            self.assertIn("provision_winboat_planck_dependencies.sh", script)
            self.assertIn("BuildCompleteSkyrimTogetherVR-Windows.ps1", script)
            self.assertIn("edbb4945544718054279c9f949ac689e735b13c8efcd3272b6f74e2398dd5d53", script)
            self.assertNotIn("& .\\BuildAuditCollectSkyrimTogetherVR-Windows.bat --gameplay", script)
        candidate = (TOOLS / "build_winboat_candidate.sh").read_text(encoding="utf-8")
        self.assertIn("--require-patched-planck-interface002", candidate)

        provisioner = (TOOLS / "provision_winboat_planck_dependencies.sh").read_text(encoding="utf-8")
        self.assertIn("7349946401a820784fc86aa13bc667def6c409ed938865b01c8e6c3d86692555", provisioner)
        self.assertIn("edbb4945544718054279c9f949ac689e735b13c8efcd3272b6f74e2398dd5d53", provisioner)
        self.assertIn("hash_source_tree.py", provisioner)
        self.assertIn("--self-check", provisioner)
        self.assertIn("STVR_SKSEVR_SOURCE_FILE_COUNT", provisioner)
        self.assertIn("Quarantined untrusted durable guest SKSEVR source tree", provisioner)

        preparer = (TOOLS / "prepare_planck_dependencies.ps1").read_text(encoding="utf-8")
        self.assertIn("Fresh patched Havok", preparer)
        self.assertIn("Fresh SKSEVR", preparer)
        self.assertIn("PLANCK_HAVOK_SOURCE_TREE_SHA256", preparer)
        self.assertIn("planck-havok-layout-access-v1", preparer)
        self.assertIn("Apply-HavokCompatibilityPatch", preparer)
        self.assertIn("PLANCK_SKSEVR_SOURCE_FILE_COUNT", preparer)

        planck_builder = (TOOLS / "build_planck_windows.ps1").read_text(encoding="utf-8")
        self.assertIn("Assert-DependencyTreesMatchProvenance", planck_builder)
        self.assertIn("stvr-planck-sksevr-build-", planck_builder)
        self.assertIn("stvr_planck_forced_build_v2", planck_builder)

        package_builder = (ROOT / "BuildSkyrimTogetherVR-Windows.ps1").read_text(encoding="utf-8")
        self.assertIn("patchedPlanckProvenance = $patchedPlanckProvenanceManifest", package_builder)
        self.assertIn("havokSourceTreeSha256", package_builder)
        self.assertIn("sksevrSourceFileCount", package_builder)

        complete_builder = (ROOT / "BuildCompleteSkyrimTogetherVR-Windows.ps1").read_text(encoding="utf-8")
        self.assertIn("--require-patched-planck-interface002", complete_builder)

    def test_windows_installer_deduplicates_case_insensitive_destinations_package_last(self) -> None:
        installer = (TOOLS / "install_local_agent_handoff_windows.ps1").read_text(encoding="utf-8")
        self.assertIn("StringComparer]::OrdinalIgnoreCase", installer)
        self.assertIn("$deduplicated[$indices[$key]] = $operation", installer)

    def test_handoff_artifact_pair_rejects_gameplay_without_forced_planck_marker(self) -> None:
        manifest = {
            "schema": local_handoff_artifacts.BUILD_MANIFEST_SCHEMA,
            "packageFlavor": "gameplay",
            "gameplay": True,
            "sourceRevision": "d" * 40,
            "sourceProvenance": {"dirty": False, "sourceRevision": "d" * 40},
        }
        with self.assertRaisesRegex(ValueError, "patched PLANCK interface002 provenance"):
            local_handoff_artifacts._validate_build_manifest(manifest, "d" * 40)


if __name__ == "__main__":
    unittest.main()
