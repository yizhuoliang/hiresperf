# Parsing Scripts

This folder contains several parsing scripts for `hiresperf`. The goal is parsing the raw file and outputing structured data to the `duckdb`.

## Dependencies

We use [`uv`](https://docs.astral.sh/uv/) to manage dependencies for these Python scripts.

To prepare the environment, please first ensure the `uv` is properly installed from their official website: https://docs.astral.sh/uv/

Then, you can run
``` bash
# cd to the `parsing` folder first
cd parsing
uv sync
```

`uv` will prepare a python virtual environment which comes with all required dependencies. 

To activate the Python venv, run `source .venv/bin/activate` in the `parsing` folder.
