# Runs ctest except tests that touch devices, drivers, services, network listeners,
# GPU, installer code or virtual display/gamepad creation (reasons: docs/research/B1_BUILD.md).
source /d/AI/ARBS-APEX/host/scripts/b1_env.sh
exclude='^(windows_dualsense_launcher|windows_dualsense_audio|test_script_playnite_waveform_helper|test_script_playnite_waveform_helper_x86|pyrowave_windows_gpu|test_component_pyrowave_udp_probe|test_component_https_listener_diagnostics|test_component_tls_client_identity|test_script_bootstrap_migration)$'
# Test-process-only settings (nothing persistent): the Korean Windows locale makes Python read
# sources as cp949, and the user execution policy blocks unsigned test .ps1 files that CI runs.
export PYTHONUTF8=1
export PSExecutionPolicyPreference=Bypass
cd "$ARBS_BUILD"
ctest --test-dir "$ARBS_BUILD" --output-on-failure --no-tests=error -E "$exclude" -j "${ARBS_CTEST_JOBS:-1}" --timeout 600 "$@"
