#!/bin/bash
set -e

# Change to project root directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_ROOT"

VENV_NAME="hrp_venv"
VENV_PATH="${PROJECT_ROOT}/${VENV_NAME}"

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

# Install packages from requirements.txt
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
cd "$PROJECT_ROOT"

# Verify installation
echo "Verifying pystream installation..."
python -c "import pystream; print(f'pystream {pystream.__version__} installed successfully')" || echo "Warning: pystream module import failed"

echo ""
echo "==================================================="
echo "Hiresperf venv setup done!"
echo "'$VENV_NAME' at path ${VENV_PATH} is now activated."
echo "===================================================" 