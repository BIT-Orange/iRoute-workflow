#!/usr/bin/env python3

import os
from pathlib import Path
import runpy
import sys


if __name__ == "__main__":
    wrapper_path = Path(__file__).resolve()
    venv_python = wrapper_path.parents[1] / ".venv" / "bin" / "python"
    if (
        os.environ.get("IROUTE_WRAPPER_REEXEC") != "1"
        and venv_python.exists()
        and Path(sys.executable) != venv_python
    ):
        env = dict(os.environ)
        env["IROUTE_WRAPPER_REEXEC"] = "1"
        os.execve(
            str(venv_python),
            [str(venv_python), str(wrapper_path), *sys.argv[1:]],
            env,
        )

    runpy.run_path(
        str(wrapper_path.parent / "checks" / "check_results.py"),
        run_name="__main__",
    )
