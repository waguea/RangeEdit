if(NOT DEFINED RANGEEDIT_EXECUTABLE)
    message(FATAL_ERROR "RANGEEDIT_EXECUTABLE is required")
endif()

if(NOT DEFINED CLI_TEST_DIR)
    message(FATAL_ERROR "CLI_TEST_DIR is required")
endif()

file(REMOVE_RECURSE "${CLI_TEST_DIR}")
file(MAKE_DIRECTORY "${CLI_TEST_DIR}")

macro(check_cli name expected_exit expected_stdout)
    execute_process(
        COMMAND "${RANGEEDIT_EXECUTABLE}" ${ARGN}
        RESULT_VARIABLE cli_result
        OUTPUT_VARIABLE cli_stdout
        ERROR_VARIABLE cli_stderr
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )

    if(NOT cli_result EQUAL ${expected_exit})
        message(FATAL_ERROR
            "${name}: expected exit ${expected_exit}, got ${cli_result}\n"
            "stderr:\n${cli_stderr}")
    endif()

    if(NOT cli_stdout STREQUAL "${expected_stdout}")
        message(FATAL_ERROR
            "${name}: stdout mismatch\n"
            "--- expected ---\n${expected_stdout}\n"
            "--- actual ---\n${cli_stdout}\n"
            "--- stderr ---\n${cli_stderr}")
    endif()
endmacro()

check_cli("version" 0 "0.1.0" --version)

set(basic "${CLI_TEST_DIR}/basic.txt")
file(WRITE "${basic}" "one needle two needle\n")
check_cli(
    "scan literal"
    0
    "${basic}:1:5: offset=4 length=6\n${basic}:1:16: offset=15 length=6"
    scan --pattern needle "${basic}")

string(CONCAT basic_json
    "[{\"file\":\"${basic}\",\"line\":1,\"column\":5,\"offset\":4,\"length\":6},"
    "{\"file\":\"${basic}\",\"line\":1,\"column\":16,\"offset\":15,\"length\":6}]")
check_cli(
    "scan literal json"
    0
    "${basic_json}"
    scan --pattern needle --json "${basic}")

check_cli(
    "scan max"
    0
    "${basic}:1:5: offset=4 length=6"
    scan --pattern needle --max 1 "${basic}")

set(case_file "${CLI_TEST_DIR}/case.txt")
file(WRITE "${case_file}" "NeEdLe needle\n")
check_cli(
    "scan ignore case"
    0
    "${case_file}:1:1: offset=0 length=6\n${case_file}:1:8: offset=7 length=6"
    scan --pattern needle --ignore-case "${case_file}")

set(whole_word "${CLI_TEST_DIR}/whole_word.txt")
file(WRITE "${whole_word}" "cat scatter cat\n")
check_cli(
    "scan whole word"
    0
    "${whole_word}:1:1: offset=0 length=3\n${whole_word}:1:13: offset=12 length=3"
    scan --pattern cat --whole-word "${whole_word}")

set(edit_file "${CLI_TEST_DIR}/edit.txt")
file(WRITE "${edit_file}" "fooXbarXfoo")
check_cli(
    "replace stdout"
    0
    "bazXbarXbaz"
    replace --pattern foo --replacement baz "${edit_file}")
check_cli(
    "delete stdout"
    0
    "XbarX"
    delete --pattern foo "${edit_file}")

set(dry_file "${CLI_TEST_DIR}/dry.txt")
file(WRITE "${dry_file}" "foo foo")
check_cli(
    "replace dry run json"
    0
    "{\"edit_count\":2,\"removed_units\":6,\"inserted_units\":6,\"output_units\":7,\"edits\":[{\"start\":0,\"length\":3},{\"start\":4,\"length\":3}]}"
    replace --pattern foo --replacement baz --dry-run --json "${dry_file}")

set(output_file "${CLI_TEST_DIR}/output.txt")
check_cli(
    "replace output"
    0
    ""
    replace --pattern foo --replacement baz --output "${output_file}" "${edit_file}")
file(READ "${output_file}" output_contents)
if(NOT output_contents STREQUAL "bazXbarXbaz")
    message(FATAL_ERROR "replace output: unexpected file contents '${output_contents}'")
endif()
if(EXISTS "${output_file}.rangeedit.tmp")
    message(FATAL_ERROR "replace output: temporary file was not removed")
endif()

set(same_file "${CLI_TEST_DIR}/same.txt")
file(WRITE "${same_file}" "foo foo")
check_cli(
    "replace same file"
    0
    ""
    replace --pattern foo --replacement bar --output "${same_file}" "${same_file}")
file(READ "${same_file}" same_contents)
if(NOT same_contents STREQUAL "bar bar")
    message(FATAL_ERROR "replace same file: unexpected file contents '${same_contents}'")
endif()

string(REPEAT "a" 65534 large_prefix)
set(large_file "${CLI_TEST_DIR}/large.txt")
file(WRITE "${large_file}" "${large_prefix}\nneedle end\n")
check_cli(
    "scan across chunk boundary"
    0
    "${large_file}:2:1: offset=65535 length=6"
    scan --pattern needle "${large_file}")

string(REPEAT "a" 65533 whole_word_prefix)
set(whole_word_large "${CLI_TEST_DIR}/whole_word_large.txt")
file(WRITE "${whole_word_large}" "${whole_word_prefix} cat scatter cat\n")
check_cli(
    "scan whole word across chunk boundary"
    0
    "${whole_word_large}:1:65535: offset=65534 length=3\n${whole_word_large}:1:65547: offset=65546 length=3"
    scan --pattern cat --whole-word "${whole_word_large}")

if(CLI_TEST_REGEX)
    check_cli(
        "scan regex"
        0
        "${basic}:1:5: offset=4 length=6\n${basic}:1:16: offset=15 length=6"
        scan --regex --pattern needle "${basic}")
endif()

check_cli("missing pattern" 2 "" scan "${basic}")
check_cli("missing replacement" 2 "" replace --pattern foo "${edit_file}")
check_cli("scan replacement" 2 "" scan --pattern foo --replacement bar "${edit_file}")
check_cli(
    "dry run output"
    2
    ""
    replace --pattern foo --replacement bar --dry-run --output "${output_file}" "${edit_file}")
check_cli(
    "replace json without dry run"
    2
    ""
    replace --pattern foo --replacement bar --json "${edit_file}")
check_cli("invalid max" 2 "" scan --pattern foo --max nope "${edit_file}")
check_cli("unknown command" 2 "" explode --pattern foo "${edit_file}")
check_cli("missing file" 2 "" scan --pattern foo "${CLI_TEST_DIR}/does-not-exist.txt")
