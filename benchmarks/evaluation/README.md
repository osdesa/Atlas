# Atlas evaluation studies

Evaluation studies are separate from benchmark execution suites. A study names
one immutable suite, the expected environments, predeclared variant contrasts,
absolute diagnostic groups, uncertainty settings, and practical-effect
threshold. `atlas_bench` continues to produce version-two raw runs and
summaries; `tools/atlas_evaluation.py` validates and analyzes those records.

The final study is `final-v1/study.json`. Its two raw result bundles are release
assets rather than tracked repository files. Each bundle contains:

```text
bundle/
  bundle-manifest.json
  capture/
    commands.log
    environment-capture.json
    vulkaninfo.txt
  results/
    suite.resolved.json
    environment.resolved.json
    runs.jsonl
    runs.csv
    tasks.csv
    comparisons.json
    comparisons.csv
```

Collect one declared environment from a clean checkout and an explicit Vulkan
ICD with:

```bash
python3 tools/atlas_evaluation.py run \
  --study benchmarks/evaluation/final-v1/study.json \
  --environment-file benchmarks/environments/milestone-16-intel-xe.json \
  --icd /absolute/path/to/intel_icd.json \
  --output-dir build/final-intel
```

`run` refuses an existing output directory or dirty worktree. It performs a
clean release build, the complete test suite, the `atlas` example, and the full
canonical benchmark before producing a compressed bundle. Repeat with the
Lavapipe environment and ICD, then analyze both result directories:

```bash
python3 tools/atlas_evaluation.py analyze \
  --study benchmarks/evaluation/final-v1/study.json \
  --results build/final-intel/results \
  --results build/final-lavapipe/results \
  --output-dir build/final-analysis
```

The checked artifact index can later reproduce the published analysis without
rerunning the workloads:

```bash
python3 tools/atlas_evaluation.py verify \
  --study benchmarks/evaluation/final-v1/study.json \
  --artifact-index benchmarks/evaluation/final-v1/artifacts.json \
  --output-dir build/published-evaluation
```
