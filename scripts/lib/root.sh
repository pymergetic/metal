# Metal package root — source from any script under scripts/.
# shellcheck shell=bash
_pm_lib_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${_pm_lib_dir}/../.." && pwd)"
unset _pm_lib_dir
