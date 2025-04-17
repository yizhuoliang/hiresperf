#!/bin/bash

# Get the directory of this script and the project root
THIS_DIR="$( cd "$( dirname "${BASH_SOURCE[0]:-${(%):-%x}}" )" >/dev/null 2>&1 && pwd )"
PROJ_ROOT="${THIS_DIR}/.."

# Save current directory and change to project root
pushd "${PROJ_ROOT}" >> /dev/null

VENV_NAME="hrp_venv"
VENV_PATH="${PROJ_ROOT}/${VENV_NAME}"

# Prevent nested environment prompts
export VIRTUAL_ENV_DISABLE_PROMPT=1

# Create virtual environment if it doesn't exist
if [ ! -d "$VENV_PATH" ]; then
  echo "Creating virtual environment '$VENV_NAME'..."
  python3 -m venv "$VENV_PATH"
else
  echo "Virtual environment '$VENV_NAME' already exists."
fi

# Activate the virtual environment
echo "Activating virtual environment..."
source "${VENV_PATH}/bin/activate"

# Install packages from requirements.txt if not installed
echo "Installing packages from requirements.txt..."
pip install -U pip setuptools wheel
pip install -r requirements.txt

# Install pystream from git submodule
echo "Installing pystream from git submodule..."
if [ ! -d "deps/pystream/pystream" ]; then
  echo "Error: pystream submodule doesn't seem to be initialized."
  echo "Try running: git submodule update --init --recursive"
  exit 1
fi

# Build and install pystream
cd deps/pystream
pip install -e .
cd "$PROJ_ROOT"

# Verify installation
echo "Verifying pystream installation..."
python -c "import pystream; print(f'pystream {pystream.__version__} installed successfully')" || echo "Warning: pystream module import failed"

# Set up Invoke tab-completion
_complete_invoke() {
    local candidates
    candidates=`invoke --complete -- ${COMP_WORDS[*]}`
    COMPREPLY=( $(compgen -W "${candidates}" -- $2) )
}

# If running from zsh, run autoload for tab completion
if [ "$(ps -o comm= -p $$)" = "zsh" ]; then
    autoload bashcompinit
    bashcompinit
fi
complete -F _complete_invoke -o default invoke inv

# Pick up project-specific binaries
export PATH="${PROJ_ROOT}/bin:${PATH}"

# Set custom prompt
export PS1="(${VENV_NAME}) $PS1"

echo ""
echo "==================================================="
echo "Hiresperf venv setup done!"
echo "'$VENV_NAME' at path ${VENV_PATH} is now activated."
echo "==================================================="

# Return to original directory
popd >> /dev/null 