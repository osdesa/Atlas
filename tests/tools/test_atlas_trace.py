import json
import pathlib
import sys
import tempfile
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "tools"))
import atlas_trace


class AtlasTraceTests(unittest.TestCase):
    def test_timeline_refuses_to_overwrite_an_existing_file(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            output = root / "timeline.svg"
            output.write_text("existing", encoding="utf-8")
            with self.assertRaisesRegex(atlas_trace.TraceError, "already exists"):
                atlas_trace.render_timeline([], output)

    def test_trace_reader_rejects_a_symlink(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            original = root / "trace.jsonl"
            original.write_text(json.dumps(atlas_trace.EXPECTED_HEADER) + "\n", encoding="utf-8")
            link = root / "trace-link.jsonl"
            link.symlink_to(original)
            with self.assertRaisesRegex(atlas_trace.TraceError, "regular file"):
                atlas_trace.read_records(link)


if __name__ == "__main__":
    unittest.main()
