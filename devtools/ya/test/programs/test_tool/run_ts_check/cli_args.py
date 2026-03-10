import argparse


class CliArgs:
    source_root: str
    build_root: str
    output_dir: str
    tracefile: str
    log_path: str
    target_path: str
    nodejs: str
    script_name: str
    test_type: str
    files: list[str]


def parse_args(args=None) -> CliArgs:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", dest="source_root", required=True)
    parser.add_argument("--build-root", dest="build_root", required=True)
    parser.add_argument("--output-dir", dest="output_dir", required=True)
    parser.add_argument("--tracefile", help="Path to the output trace log", required=True)
    parser.add_argument("--log-path", dest="log_path", help="Log file path", required=True)
    parser.add_argument("--target_path", dest="target_path", help="Path to the test for", required=True)
    parser.add_argument("--nodejs", dest="nodejs", help="Path to the Node.JS resource dir", required=True)
    parser.add_argument("--script-name", dest="script_name", help="Name of the script to run", required=True)
    parser.add_argument("--test-type", dest="test_type", help="Type of the test for report", required=True)
    parser.add_argument("files", nargs='*')
    return parser.parse_args(args)
