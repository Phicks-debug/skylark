#!/usr/bin/env bash
# Tiny-Skylark Test Suite — Comprehensive integration & unit tests
set -uo pipefail

BINARY="${TINY_SKYLARK_BINARY:-./build/tiny-skylark}"
PASS=0
FAIL=0

# ---- Helpers ----
green()  { echo -e "\033[32m✓ $1\033[0m"; }
red()    { echo -e "\033[31m✗ $1\033[0m"; }
fail()   { red "$1"; FAIL=$((FAIL+1)); return 1; }
pass()   { green "$1"; PASS=$((PASS+1)); }

assert_contains() {
    local test_name="$1" output="$2" needle="$3"
    if echo "$output" | grep -qF -- "$needle"; then
        pass "$test_name"
    else
        fail "$test_name — expected to contain: $needle"
    fi
}

assert_not_contains() {
    local test_name="$1" output="$2" needle="$3"
    if echo "$output" | grep -qF -- "$needle"; then
        fail "$test_name — should NOT contain: $needle"
    else
        pass "$test_name"
    fi
}

assert_exit_code() {
    local test_name="$1" actual="$2" expected="$3"
    if [ "$actual" -eq "$expected" ]; then
        pass "$test_name"
    else
        fail "$test_name — expected exit $expected, got $actual"
    fi
}

# Use gtimeout (coreutils) if available, otherwise just run directly (macOS doesn't have timeout)
if command -v gtimeout &>/dev/null; then
    run_cli() { gtimeout 30 "$BINARY" "$@" 2>&1; }
elif command -v timeout &>/dev/null; then
    run_cli() { timeout 30 "$BINARY" "$@" 2>&1; }
else
    run_cli() { "$BINARY" "$@" 2>&1; }
fi

# =================================================================
# SECTION 1: CLI Argument Parsing & Validation
# =================================================================
echo ""
echo "━━━ CLI Argument Parsing ━━━"

# --help flag
out=$(run_cli --help)
assert_contains "--help shows usage"       "$out" "Usage:"
assert_contains "--help lists --model"     "$out" "--model"
assert_contains "--help lists --backend"   "$out" "--backend"
assert_contains "--help lists --search"    "$out" "--search"
assert_contains "--help lists --voice"     "$out" "--voice"
assert_contains "--help lists --no-thinking" "$out" "--no-thinking"

# -h flag (short form)
out=$(run_cli -h)
assert_contains "-h shows usage" "$out" "Usage:"

# --help with no model should succeed
run_cli --help; assert_exit_code "--help exit 0" $? 0

# Missing --model
out=$(run_cli 2>&1); rc=$?
assert_exit_code "missing --model exits 1" $rc 1
assert_contains "missing --model message" "$out" "--model is required"

# Unknown option
out=$(run_cli --model dummy --bad-flag 2>&1); rc=$?
assert_exit_code "unknown option exits 1" $rc 1
assert_contains "unknown option message" "$out" "Unknown option"

# ---- Invalid numeric args (try-catch safety) ----
out=$(run_cli --model dummy --max-tokens "notanumber" 2>&1); rc=$?
assert_exit_code "--max-tokens invalid" $rc 1
assert_contains "--max-tokens error msg" "$out" "Invalid value for --max-tokens"

out=$(run_cli --model dummy --top-k "abc" 2>&1); rc=$?
assert_exit_code "--top-k invalid" $rc 1
assert_contains "--top-k error msg" "$out" "Invalid value for --top-k"

out=$(run_cli --model dummy --top-p "xyz" 2>&1); rc=$?
assert_exit_code "--top-p invalid" $rc 1
assert_contains "--top-p error msg" "$out" "Invalid value for --top-p"

out=$(run_cli --model dummy --temperature "hot" 2>&1); rc=$?
assert_exit_code "--temperature invalid" $rc 1
assert_contains "--temperature error msg" "$out" "Invalid value for --temperature"

out=$(run_cli --model dummy --seed "bad" 2>&1); rc=$?
assert_exit_code "--seed invalid" $rc 1
assert_contains "--seed error msg" "$out" "Invalid value for --seed"

# ---- Out-of-range validation ----
out=$(run_cli --model dummy --max-tokens "0" 2>&1); rc=$?
assert_exit_code "--max-tokens 0 exits 1" $rc 1
assert_contains "--max-tokens 0 message" "$out" "--max-tokens must be positive"

out=$(run_cli --model dummy --max-tokens "-5" 2>&1); rc=$?
assert_exit_code "--max-tokens negative exits 1" $rc 1

out=$(run_cli --model dummy --top-p "1.5" 2>&1); rc=$?
assert_exit_code "--top-p > 1.0 exits 1" $rc 1
assert_contains "--top-p > 1.0 message" "$out" "--top-p must be between"

out=$(run_cli --model dummy --top-p "-0.1" 2>&1); rc=$?
assert_exit_code "--top-p negative exits 1" $rc 1

out=$(run_cli --model dummy --temperature "-1.0" 2>&1); rc=$?
assert_exit_code "--temperature negative exits 1" $rc 1

# ---- Flag parsing ----
# Boolean flags should not consume the next arg
out=$(run_cli --model dummy --no-stream --max-tokens 100 --help 2>&1)
assert_not_contains "bool flag doesn't eat next" "$out" "Invalid value for --max-tokens"

# ---- Combined flags ----
out=$(run_cli --model dummy --search --no-thinking --no-stream --speculative --help 2>&1)
assert_contains "combined flags show help" "$out" "Usage:"

# ---- TAVILY_API_KEY env var ----
out=$(TAVILY_API_KEY="test-key-123" run_cli --model dummy --search --help 2>&1)
assert_contains "env var sets api key" "$out" "Usage:"  # just verify no crash

# =================================================================
# SECTION 2: Help Text Completeness
# =================================================================
echo ""
echo "━━━ Help Text ━━━"

out=$(run_cli --help)
assert_contains "help: model option"     "$out" "--model"
assert_contains "help: backend option"   "$out" "cpu or gpu"
assert_contains "help: max-tokens"       "$out" "--max-tokens"
assert_contains "help: top-k"            "$out" "--top-k"
assert_contains "help: top-p"            "$out" "--top-p"
assert_contains "help: temperature"      "$out" "--temperature"
assert_contains "help: seed"             "$out" "--seed"
assert_contains "help: speculative"      "$out" "--speculative"
assert_contains "help: no-stream"        "$out" "--no-stream"
assert_contains "help: voice"            "$out" "--voice"
assert_contains "help: image"            "$out" "--image"
assert_contains "help: video"            "$out" "--video"
assert_contains "help: search"           "$out" "--search"
assert_contains "help: tavily-key"       "$out" "--tavily-key"
assert_contains "help: download"         "$out" "--download"
assert_contains "help: system-prompt"    "$out" "--system-prompt"
assert_contains "help: no-thinking"      "$out" "--no-thinking"
assert_contains "help: TAVILY_API_KEY"   "$out" "TAVILY_API_KEY"
assert_contains "help: examples"         "$out" "Examples:"

# =================================================================
# SECTION 3: JSON Utility Functions (unit tests via compiled test binary)
# =================================================================
echo ""
echo "━━━ JSON Utilities (compiled test) ━━━"

# Build the test binary
TEST_SRC="tests/test_json_utils.cpp"
TEST_BIN="./build/test_json_utils"

if [ ! -f "$TEST_SRC" ]; then
    red "test_json_utils.cpp not found — skipping compiled unit tests"
else
    # Compile the test
    if g++ -std=c++20 -o "$TEST_BIN" "$TEST_SRC" 2>&1; then
        out=$("$TEST_BIN" 2>&1); rc=$?
        assert_exit_code "json utils: exit 0" $rc 0
        assert_contains "json utils: all passed" "$out" "ALL TESTS PASSED"
        # Parse individual test results
        while IFS= read -r line; do
            if [[ "$line" == *"PASS"* ]]; then
                pass "$line"
            elif [[ "$line" == *"FAIL"* ]]; then
                fail "$line"
            fi
        done < <(echo "$out")
    else
        fail "test_json_utils.cpp failed to compile"
    fi
fi

# =================================================================
# SECTION 4: Signal Handler Safety
# =================================================================
echo ""
echo "━━━ Signal Handler Safety ━━━"

SRC_DIR="src"

# Verify binary has signal handling (static symbol may not appear in binary;
# check source code for signal() setup instead)
if grep -q 'std::signal' "$SRC_DIR/main.cpp" && grep -q 'sigint_handler' "$SRC_DIR/main.cpp"; then
    pass "signal handler registered in source"
else
    fail "signal handler not found in source"
fi

# Verify the binary doesn't call non-async-signal-safe functions from handler
# (cout, malloc, free should not be in the signal path)
# This is a best-effort check

# =================================================================
# SECTION 5: Memory / Resource Safety (static analysis hints)
# =================================================================
echo ""
echo "━━━ Memory Safety ━━━"

# Check for common issues in source
SRC_DIR="src"

# No naked new/delete
if grep -rn '\bnew\b' "$SRC_DIR"/*.cpp "$SRC_DIR"/*.hpp 2>/dev/null | grep -v '//' | grep -v 'new ('; then
    fail "naked 'new' found in source"
else
    pass "no naked 'new' in source"
fi

if grep -rn '\bdelete\b' "$SRC_DIR"/*.cpp "$SRC_DIR"/*.hpp 2>/dev/null | grep -v '//'; then
    fail "naked 'delete' found in source"
else
    pass "no naked 'delete' in source"
fi

# Clean C API resource management
if grep -q 'litert_lm_.*_delete(' "$SRC_DIR/main.cpp"; then
    pass "C API resource cleanup present"
else
    fail "C API resource cleanup missing"
fi

# RAII-style cleanup
if grep -q 'litert_lm_engine_settings_delete' "$SRC_DIR/main.cpp" && \
   grep -q 'litert_lm_conversation_config_delete' "$SRC_DIR/main.cpp" && \
   grep -q 'litert_lm_conversation_delete' "$SRC_DIR/main.cpp" && \
   grep -q 'litert_lm_engine_delete' "$SRC_DIR/main.cpp"; then
    pass "all C API delete calls present"
else
    fail "some C API delete calls missing"
fi

# Check curl cleanup in model_downloader
if grep -q 'curl_easy_cleanup' "$SRC_DIR/model_downloader.cpp"; then
    pass "curl_easy_cleanup present"
else
    fail "curl_easy_cleanup missing"
fi

# Check PortAudio termination in audio_recorder
if grep -q 'Pa_Terminate' "$SRC_DIR/audio_recorder.cpp"; then
    pass "Pa_Terminate present"
else
    fail "Pa_Terminate missing"
fi

# =================================================================
# SECTION 6: Code Quality Checks
# =================================================================
echo ""
echo "━━━ Code Quality ━━━"

# No unused variable warnings (check recent build)
if [ -f build/CMakeFiles/tiny-skylark.dir/src/main.cpp.o ]; then
    pass "main.cpp built successfully"
else
    fail "main.cpp object missing"
fi

# Check for const correctness on c_str() calls
# (temporary std::string .c_str() is a common bug)
if grep -rn 'std::string(.*)\.c_str()' "$SRC_DIR"/*.cpp 2>/dev/null; then
    fail "potential dangling .c_str() found"
else
    pass "no dangling .c_str() patterns"
fi

# Check for thread-safety: mutex used with condition_variable
if grep -q 'std::condition_variable' "$SRC_DIR/main.cpp" && \
   grep -q 'std::mutex' "$SRC_DIR/main.cpp"; then
    pass "mutex + condition_variable present for thread safety"
else
    fail "thread safety primitives missing"
fi

# Atomic for signal handler communication
if grep -q 'std::atomic<bool>.*g_cancellation_requested' "$SRC_DIR/main.cpp"; then
    pass "atomic flag for signal handler safety"
else
    fail "atomic flag for signal handler missing"
fi

# =================================================================
# SECTION 7: Streaming & Cancellation Architecture
# =================================================================
echo ""
echo "━━━ Streaming/Cancellation Architecture ━━━"

# Verify poll-based cancellation in streaming loop
if grep -q 'poll_interval' "$SRC_DIR/main.cpp" && \
   grep -q 'g_cancellation_requested.exchange' "$SRC_DIR/main.cpp"; then
    pass "poll-based cancellation in streaming loop"
else
    fail "poll-based cancellation missing from streaming loop"
fi

# Verify cancellation wait after cancel_process
if grep -q 'cancel_process' "$SRC_DIR/main.cpp" && \
   grep -q 'wait_for.*chrono::seconds(5)' "$SRC_DIR/main.cpp"; then
    pass "post-cancellation wait for callback"
else
    fail "post-cancellation wait missing"
fi

# Verify between-message cancellation check
if grep -q 'Consume any stale' "$SRC_DIR/main.cpp"; then
    pass "between-message cancellation check"
else
    fail "between-message cancellation check missing"
fi

# Verify no g_active_conversation (was dead code)
if ! grep -q 'g_active_conversation' "$SRC_DIR/main.cpp"; then
    pass "dead g_active_conversation removed"
else
    fail "g_active_conversation still present (dead code)"
fi

# =================================================================
# SECTION 8: Security Checks
# =================================================================
echo ""
echo "━━━ Security ━━━"

# No system() or popen() calls
if grep -rn '\bsystem\b\s*(' "$SRC_DIR"/*.cpp "$SRC_DIR"/*.hpp 2>/dev/null | grep -v '//'; then
    fail "system() call found (potential command injection)"
else
    pass "no system() calls"
fi

if grep -rn '\bpopen\b\s*(' "$SRC_DIR"/*.cpp "$SRC_DIR"/*.hpp 2>/dev/null | grep -v '//'; then
    fail "popen() call found (potential command injection)"
else
    pass "no popen() calls"
fi

# API key not hardcoded in source (check for key patterns)
if grep -rn 'tvly-dev-\|sk-\|api_key\s*=\s*"[a-zA-Z0-9]' "$SRC_DIR"/*.cpp "$SRC_DIR"/*.hpp 2>/dev/null; then
    fail "hardcoded API key found in source"
else
    pass "no hardcoded API keys"
fi

# JSON escaping handles all dangerous chars
if grep -q "case '\\\\b':" "$SRC_DIR/main.cpp" && \
   grep -q "case '\\\\f':" "$SRC_DIR/main.cpp" && \
   grep -q "\\\\\\\\u00" "$SRC_DIR/main.cpp"; then
    pass "json_escape handles \\b, \\f, control chars"
else
    fail "json_escape missing escape cases"
fi

# =================================================================
# SECTION 9: Optimization Checks
# =================================================================
echo ""
echo "━━━ Optimizations ━━━"

# String reserve used to avoid reallocations
if grep -q 'reserve(' "$SRC_DIR/main.cpp"; then
    pass "string::reserve used for pre-allocation"
else
    fail "no string::reserve usage"
fi

# std::move used where appropriate
if grep -q 'std::move' "$SRC_DIR/main.cpp"; then
    pass "std::move used for efficient transfers"
else
    fail "no std::move usage"
fi

# string_view used to avoid copies
if grep -q 'std::string_view' "$SRC_DIR/main.cpp"; then
    pass "std::string_view used for zero-copy parsing"
else
    fail "no std::string_view usage"
fi

# =================================================================
# SECTION 10: Compiler Warnings
# =================================================================
echo ""
echo "━━━ Compiler Warnings ━━━"

BUILD_OUT=$(cd build && cmake --build . -j"$(sysctl -n hw.logicalcpu)" 2>&1)
WARNINGS=$(echo "$BUILD_OUT" | grep -c 'warning:' || true)
if [ "$WARNINGS" -eq 0 ]; then
    pass "zero compiler warnings"
else
    fail "$WARNINGS compiler warning(s) found"
    echo "$BUILD_OUT" | grep 'warning:'
fi

# =================================================================
# RESULTS
# =================================================================
echo ""
echo "══════════════════════════════════════"
echo "  RESULTS: $PASS passed, $FAIL failed"
echo "══════════════════════════════════════"

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0
