import copy
import hashlib
import json
import pathlib
import sys
import tarfile
import tempfile
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "tools"))
import atlas_evaluation as evaluation


class AtlasEvaluationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temporary.name)
        self.suite = {
            "schema_version": 1,
            "suite_id": "fixture",
            "seeds": [1, 2],
            "warmup_runs": 0,
            "repetitions": 1,
            "worker_count": 1,
            "cases": [
                {
                    "case_id": "cpu",
                    "workload": {
                        "cpu": {"task_count": 1, "iterations": 1},
                        "gpu": {"task_count": 0, "workgroups": {"x": 1, "y": 1, "z": 1}},
                        "dependencies": {"shape": "independent"},
                        "priorities": {"assignment": "cycle", "values": [0]},
                        "bursts": {"count": 1},
                    },
                    "reference_variant": "direct",
                    "variants": [
                        {"variant_id": "direct", "execution": "direct"},
                        {
                            "variant_id": "fifo",
                            "execution": "scheduled",
                            "policy": {"type": "fifo"},
                            "slice_workgroups": None,
                        },
                    ],
                }
            ],
        }
        self.suite_path = self.root / "suite.json"
        self.suite_path.write_text(json.dumps(self.suite), encoding="utf-8")
        digest = hashlib.sha256(self.suite_path.read_bytes()).hexdigest()
        self.study_value = {
            "schema_version": 1,
            "study_id": "fixture-study",
            "suite_path": "suite.json",
            "suite_sha256": digest,
            "expected_environments": ["one", "two"],
            "analysis": {"confidence_level": 0.95, "bootstrap_resamples": 100, "practical_effect_percent": 5.0},
            "questions": [
                {
                    "question_id": "rq",
                    "title": "Fixture question",
                    "contrasts": [
                        {
                            "contrast_id": "fifo-effect",
                            "case_id": "cpu",
                            "reference_variant": "direct",
                            "candidate_variant": "fifo",
                            "metrics": ["completion_time_us", "throughput_tasks_per_second"],
                        }
                    ],
                    "absolute_groups": [
                        {
                            "group_id": "active",
                            "case_id": "cpu",
                            "variants": ["fifo"],
                            "metrics": ["control_active_fraction"],
                        }
                    ],
                }
            ],
        }
        self.study_path = self.root / "study.json"
        self.study_path.write_text(json.dumps(self.study_value), encoding="utf-8")

    def tearDown(self):
        self.temporary.cleanup()

    @staticmethod
    def _run(environment, variant, seed, completion):
        metrics = {
            "throughput_tasks_per_second": 1_000_000.0 / completion,
            "response_latency": {"mean_us": float(completion), "p50_us": float(completion), "p95_us": float(completion), "max_us": float(completion)},
            "ready_wait": {"mean_us": 1.0, "p50_us": 1.0, "p95_us": 1.0, "max_us": 1.0},
            "selection_bypass_mean": None if variant == "direct" else 0.0,
            "selection_bypass_max": None if variant == "direct" else 0.0,
            "immediate_slice_switch_mean_us": None,
            "cpu_busy_fraction": 0.5,
            "gpu_host_busy_fraction": None,
            "gpu_timestamp_supported": False,
            "gpu_timestamp_busy_fraction": None,
            "cpu_jain_fairness": 1.0,
            "gpu_jain_fairness": None,
        }
        return {
            "run_schema_version": 2,
            "suite_id": "fixture",
            "case_id": "cpu",
            "variant_id": variant,
            "execution": "direct" if variant == "direct" else "scheduled",
            "seed": seed,
            "repetition": 0,
            "execution_order": 0,
            "environment": environment,
            "result": {"status": "Success", "executed_task_count": 1, "completion_time_us": completion, "control_active_us": 1, "control_active_fraction": 0.1},
            "metrics": metrics,
            "tasks": [
                {
                    "index": 0,
                    "name": "cpu-0",
                    "resource": "cpu",
                    "priority": 0,
                    "burst": 0,
                    "state": "Success",
                    "execution_us": completion,
                    "device_execution_ns": None,
                    "ready_wait_us": 1,
                    "response_us": completion,
                    "selection_bypasses": None if variant == "direct" else 0,
                    "completed_work_units": 1,
                    "total_work_units": 1,
                }
            ],
        }

    def _results(self, environment_id):
        root = self.root / environment_id
        root.mkdir()
        environment = {"git_revision": "abc123", "user_metadata": {"environment_id": environment_id}}
        (root / "suite.resolved.json").write_text(json.dumps(self.suite), encoding="utf-8")
        (root / "environment.resolved.json").write_text(json.dumps(environment), encoding="utf-8")
        runs = []
        for seed in (1, 2):
            runs.append(self._run(environment, "direct", seed, 100 + seed))
            runs.append(self._run(environment, "fifo", seed, 120 + seed))
        (root / "runs.jsonl").write_text("".join(json.dumps(run) + "\n" for run in runs), encoding="utf-8")
        for name in ("runs.csv", "tasks.csv", "comparisons.json", "comparisons.csv"):
            (root / name).write_text("fixture\n", encoding="utf-8")
        return root

    def test_analysis_is_paired_and_deterministic(self):
        study = evaluation.load_study(self.study_path)
        results = [evaluation.load_results(study, self._results("one")), evaluation.load_results(study, self._results("two"))]
        first = evaluation.calculate(study, results)
        second = evaluation.calculate(study, results)
        self.assertEqual(first, second)
        metric = first["questions"][0]["contrasts"][0]["environments"]["one"][0]
        self.assertEqual(metric["paired_sample_count"], 2)
        self.assertTrue(metric["stable"])
        self.assertTrue(metric["material"])
        self.assertFalse(metric["beneficial"])

        output = self.root / "analysis"
        evaluation.write_analysis(output, first)
        self.assertTrue((output / "evaluation.json").is_file())
        self.assertTrue((output / "tables" / "rq.md").is_file())
        self.assertIn("<svg", (output / "plots" / "rq.svg").read_text(encoding="utf-8"))

    def test_missing_duplicate_and_failed_runs_are_rejected(self):
        study = evaluation.load_study(self.study_path)
        root = self._results("one")
        lines = (root / "runs.jsonl").read_text(encoding="utf-8").splitlines()
        (root / "runs.jsonl").write_text("\n".join(lines[:-1]) + "\n", encoding="utf-8")
        with self.assertRaisesRegex(evaluation.EvaluationError, "incomplete"):
            evaluation.load_results(study, root)

        root = self._results("two")
        second_lines = (root / "runs.jsonl").read_text(encoding="utf-8").splitlines()
        (root / "runs.jsonl").write_text("\n".join(second_lines + [second_lines[0]]) + "\n", encoding="utf-8")
        with self.assertRaisesRegex(evaluation.EvaluationError, "Duplicate"):
            evaluation.load_results(study, root)

    def test_study_rejects_unknown_metric_and_suite_digest(self):
        invalid = copy.deepcopy(self.study_value)
        invalid["questions"][0]["contrasts"][0]["metrics"] = ["unknown"]
        self.study_path.write_text(json.dumps(invalid), encoding="utf-8")
        with self.assertRaisesRegex(evaluation.EvaluationError, "unsupported metrics"):
            evaluation.load_study(self.study_path)

        invalid = copy.deepcopy(self.study_value)
        invalid["suite_sha256"] = "0" * 64
        self.study_path.write_text(json.dumps(invalid), encoding="utf-8")
        with self.assertRaisesRegex(evaluation.EvaluationError, "suite_sha256"):
            evaluation.load_study(self.study_path)

    def test_archive_extraction_rejects_parent_paths(self):
        archive = self.root / "unsafe.tar.gz"
        payload = self.root / "payload"
        payload.write_text("bad", encoding="utf-8")
        with tarfile.open(archive, "w:gz") as stream:
            stream.add(payload, arcname="../outside")
        destination = self.root / "extract"
        destination.mkdir()
        with self.assertRaisesRegex(evaluation.EvaluationError, "unsafe path"):
            evaluation._safe_extract(archive, destination)

    def test_archive_extraction_rejects_links_and_duplicate_members(self):
        archive = self.root / "unsafe-link.tar.gz"
        with tarfile.open(archive, "w:gz") as stream:
            link = tarfile.TarInfo("link")
            link.type = tarfile.SYMTYPE
            link.linkname = "target"
            stream.addfile(link)
        destination = self.root / "extract-link"
        destination.mkdir()
        with self.assertRaisesRegex(evaluation.EvaluationError, "unsafe member"):
            evaluation._safe_extract(archive, destination)

    def test_artifact_index_requires_allowlisted_https_url(self):
        study = evaluation.load_study(self.study_path)
        index = {
            "artifact_index_schema_version": 1,
            "study_id": study.study_id,
            "release_tag": "fixture",
            "artifacts": [
                {
                    "environment_id": environment_id,
                    "url": "http://example.invalid/result.tar.gz",
                    "size_bytes": 1,
                    "sha256": "0" * 64,
                    "source_revision": "abc123",
                    "suite_sha256": study.value["suite_sha256"],
                }
                for environment_id in study.environments
            ],
        }
        path = self.root / "artifacts.json"
        path.write_text(json.dumps(index), encoding="utf-8")
        with self.assertRaisesRegex(evaluation.EvaluationError, "HTTPS"):
            evaluation._load_artifact_index(path, study)

    def test_bundle_manifest_detects_tampering(self):
        study = evaluation.load_study(self.study_path)
        results = self._results("one")
        capture = self.root / "capture"
        capture.mkdir()
        (capture / "commands.log").write_text("fixture\n", encoding="utf-8")
        output = self.root / "packaged"
        output.mkdir()
        archive = evaluation._create_bundle(output, results, capture, study, "one", "abc123")
        artifact = {
            "environment_id": "one",
            "source_revision": "abc123",
            "size_bytes": archive.stat().st_size,
            "sha256": evaluation._sha256(archive),
            "suite_sha256": study.value["suite_sha256"],
            "url": "https://example.invalid/fixture.tar.gz",
        }
        destination = self.root / "unpacked"
        destination.mkdir()
        evaluation._safe_extract(archive, destination)
        bundle = destination / "bundle"
        self.assertEqual(evaluation._validate_bundle(bundle, study, artifact), bundle / "results")
        (bundle / "results" / "runs.csv").write_text("tampered\n", encoding="utf-8")
        with self.assertRaisesRegex(evaluation.EvaluationError, "failed verification"):
            evaluation._validate_bundle(bundle, study, artifact)


if __name__ == "__main__":
    unittest.main()
