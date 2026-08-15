import json
import pathlib
import subprocess
import sys
import tempfile


def run_case(latency_ms, expected_code):
    root = pathlib.Path(__file__).resolve().parents[1]
    script = root / "scripts" / "audio" / "analyze_latency_report.py"
    with tempfile.TemporaryDirectory() as directory:
        input_path = pathlib.Path(directory) / "samples.jsonl"
        output_path = pathlib.Path(directory) / "report.json"
        with input_path.open("w", encoding="utf-8") as output:
            for sequence in range(300):
                submitted = sequence * 1000
                output.write(json.dumps({
                    "sequence": sequence,
                    "sourceSubmittedMonotonicMs": submitted,
                    "detectedMonotonicMs": submitted + latency_ms,
                }) + "\n")
        result = subprocess.run(
            [sys.executable, str(script), str(input_path), str(output_path)],
            check=False,
        )
        assert result.returncode == expected_code
        report = json.loads(output_path.read_text(encoding="utf-8"))
        assert report["passed"] == (expected_code == 0)


run_case(80, 0)
run_case(180, 1)
