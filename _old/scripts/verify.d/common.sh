# Platform-agnostic verify bootstrap — source from port scripts.
# Layout: verify.d/{common,expect,suite}.sh  |  verify.d/port/<platform>/
# shellcheck shell=bash
_pm_verify_d="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "${_pm_verify_d}/../lib/root.sh"
# shellcheck disable=SC1091
source "${_pm_verify_d}/../lib/guest-package.sh"
# shellcheck disable=SC1091
source "${_pm_verify_d}/../lib/guest-pkgs.sh"
# shellcheck disable=SC1091
source "${_pm_verify_d}/../lib/linux-cmake.sh"
# shellcheck disable=SC1091
source "${_pm_verify_d}/expect.sh"
# shellcheck disable=SC1091
source "${_pm_verify_d}/suite.sh"
unset _pm_verify_d
