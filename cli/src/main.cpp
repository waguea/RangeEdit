#include "rangeedit/edit.hpp"
#include "rangeedit/literal.hpp"
#include "rangeedit/regex.hpp"
#include "rangeedit/stream.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

constexpr std::string_view kVersion = "0.1.0";

enum class Command {
    Scan,
    Replace,
    Delete,
};

void print_usage(std::ostream& output) {
    output << "RangeEdit " << kVersion << "\n\n"
           << "Usage:\n"
           << "  rangeedit scan --pattern PATTERN [options] FILE\n"
           << "  rangeedit replace --pattern PATTERN --replacement TEXT [options] FILE\n"
           << "  rangeedit delete --pattern PATTERN [options] FILE\n\n"
           << "Options:\n"
           << "  --ignore-case          Case-insensitive (ASCII literals, Unicode regex)\n"
           << "  --whole-word           Whole-word boundaries (ASCII literal, Unicode regex)\n"
           << "  --regex                Treat PATTERN as a RE2 regular expression\n"
           << "  --max N                Stop after N matches\n"
           << "  --algorithm NAME       naive | bmh | kmp | std (default: bmh)\n"
           << "  --replacement TEXT     Replacement text (replace command)\n"
           << "  --dry-run              Report edits without writing output\n"
           << "  --json                 JSON output (scan, or dry-run edits)\n"
           << "  --output FILE          Write transformed text atomically to FILE\n"
           << "  -h, --help             Show this help\n"
           << "  --version              Show the version\n";
}

std::optional<std::size_t> parse_size(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }

    std::size_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return std::nullopt;
    }

    return value;
}

std::optional<rangeedit::LiteralAlgorithm> parse_algorithm(std::string_view text) {
    if (text == "naive") {
        return rangeedit::LiteralAlgorithm::Naive;
    }
    if (text == "bmh") {
        return rangeedit::LiteralAlgorithm::Bmh;
    }
    if (text == "kmp") {
        return rangeedit::LiteralAlgorithm::Kmp;
    }
    if (text == "std") {
        return rangeedit::LiteralAlgorithm::StdSearch;
    }
    return std::nullopt;
}

std::optional<std::string> read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }

    const std::istreambuf_iterator<char> begin(input);
    const std::istreambuf_iterator<char> end;
    std::string contents(begin, end);

    if (!input.eof() && input.fail()) {
        return std::nullopt;
    }

    return contents;
}

bool write_file_atomic(const std::string& path, std::string_view contents) {
    const std::filesystem::path target(path);
    std::filesystem::path temporary = target;
    temporary += ".rangeedit.tmp";

    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            return false;
        }

        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!output) {
            return false;
        }

        output.close();
        if (!output) {
            return false;
        }
    }

    std::error_code error;
    std::filesystem::rename(temporary, target, error);
    if (error) {
        std::error_code remove_error;
        std::filesystem::remove(target, remove_error);
        error.clear();
        std::filesystem::rename(temporary, target, error);
    }

    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }

    return true;
}

void print_json_string(std::ostream& output, std::string_view text) {
    output << '"';
    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        switch (character) {
            case '"':
                output << "\\\"";
                break;
            case '\\':
                output << "\\\\";
                break;
            case '\b':
                output << "\\b";
                break;
            case '\f':
                output << "\\f";
                break;
            case '\n':
                output << "\\n";
                break;
            case '\r':
                output << "\\r";
                break;
            case '\t':
                output << "\\t";
                break;
            default:
                if (byte < 0x20U) {
                    constexpr char kHex[] = "0123456789abcdef";
                    output << "\\u00" << kHex[byte >> 4U] << kHex[byte & 0x0FU];
                } else {
                    output << character;
                }
                break;
        }
    }
    output << '"';
}

void print_match_record(
    const std::string& path,
    const rangeedit::Match& match,
    std::size_t line,
    std::size_t column,
    bool json,
    bool& first) {
    if (json) {
        if (!first) {
            std::cout << ',';
        }
        std::cout << "{\"file\":";
        print_json_string(std::cout, path);
        std::cout << ",\"line\":" << line << ",\"column\":" << column
                  << ",\"offset\":" << match.start << ",\"length\":" << match.length << '}';
        first = false;
    } else {
        std::cout << path << ':' << line << ':' << column << ": offset=" << match.start
                  << " length=" << match.length << '\n';
    }
}

void print_matches(
    const std::string& path,
    std::string_view text,
    const rangeedit::MatchList& matches,
    bool json) {
    std::size_t line = 1;
    std::size_t column = 1;
    std::size_t cursor = 0;

    if (json) {
        std::cout << '[';
    }

    bool first = true;
    for (const auto& match : matches) {
        for (; cursor < match.start && cursor < text.size(); ++cursor) {
            if (text[cursor] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }

        print_match_record(path, match, line, column, json, first);
    }

    if (json) {
        std::cout << "]\n";
    }
}

struct StreamPosition {
    std::size_t offset = 0;
    std::size_t line = 1;
    std::size_t column = 1;
};

void advance_position(
    std::string_view text,
    std::size_t end,
    std::size_t& line,
    std::size_t& column) {
    for (std::size_t i = 0; i < end; ++i) {
        if (text[i] == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }
}

struct Arguments {
    Command command = Command::Scan;
    std::string pattern;
    std::string replacement;
    std::string file;
    std::string output;
    rangeedit::MatchOptions match_options;
    rangeedit::LiteralAlgorithm algorithm = rangeedit::LiteralAlgorithm::Bmh;
    bool has_pattern = false;
    bool has_replacement = false;
    bool regex = false;
    bool dry_run = false;
    bool json = false;
    bool help = false;
    bool version = false;
};

bool scan_literal_stream(const Arguments& arguments) {
    std::ifstream input(arguments.file, std::ios::binary);
    if (!input) {
        return false;
    }

    rangeedit::LiteralStreamScanner scanner(
        arguments.pattern,
        arguments.match_options,
        arguments.algorithm);

    std::array<char, 64 * 1024> chunk{};
    std::string tail;
    StreamPosition buffer_start;
    bool first = true;

    if (arguments.json) {
        std::cout << '[';
    }

    const auto emit_matches = [&](const rangeedit::MatchList& matches, std::string_view buffer) {
        std::size_t cursor = 0;
        std::size_t line = buffer_start.line;
        std::size_t column = buffer_start.column;

        for (const auto& match : matches) {
            const auto local_start = match.start - buffer_start.offset;
            advance_position(buffer.substr(cursor), local_start - cursor, line, column);
            cursor = local_start;
            print_match_record(arguments.file, match, line, column, arguments.json, first);
        }
    };

    while (input) {
        input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto count = input.gcount();
        if (count <= 0) {
            break;
        }

        const std::string_view chunk_view(chunk.data(), static_cast<std::size_t>(count));

        std::string buffer;
        buffer.reserve(tail.size() + chunk_view.size());
        buffer.append(tail);
        buffer.append(chunk_view);

        const auto matches = scanner.feed(chunk_view);
        emit_matches(matches, buffer);

        const auto stream_end = scanner.bytes_fed();
        const auto carry_size = arguments.pattern.empty()
                                    ? 0
                                    : std::min(arguments.pattern.size() + 1, stream_end);
        const auto carry_start = stream_end - carry_size;
        const auto carry_offset = carry_start - buffer_start.offset;

        auto carry_line = buffer_start.line;
        auto carry_column = buffer_start.column;
        advance_position(buffer, carry_offset, carry_line, carry_column);

        tail.assign(buffer.data() + carry_offset, carry_size);
        buffer_start = {carry_start, carry_line, carry_column};

        if (arguments.match_options.max_matches != 0 &&
            scanner.matches_found() >= arguments.match_options.max_matches) {
            break;
        }
    }

    if (input.bad()) {
        return false;
    }

    const auto final_matches = scanner.finish();
    emit_matches(final_matches, tail);

    if (arguments.json) {
        std::cout << "]\n";
    }

    std::cerr << scanner.matches_found() << " match(es)\n";
    return true;
}

void print_plan(const rangeedit::EditPlan& plan, bool json) {
    if (json) {
        std::cout << "{\"edit_count\":" << plan.stats.edit_count
                  << ",\"removed_units\":" << plan.stats.removed_units
                  << ",\"inserted_units\":" << plan.stats.inserted_units
                  << ",\"output_units\":" << plan.stats.output_units << ",\"edits\":[";

        for (std::size_t i = 0; i < plan.edits.size(); ++i) {
            if (i != 0) {
                std::cout << ',';
            }
            std::cout << "{\"start\":" << plan.edits[i].start
                      << ",\"length\":" << plan.edits[i].length << '}';
        }

        std::cout << "]}\n";
        return;
    }

    std::cout << "edit_count=" << plan.stats.edit_count
              << " removed_units=" << plan.stats.removed_units
              << " inserted_units=" << plan.stats.inserted_units
              << " output_units=" << plan.stats.output_units << '\n';

    for (std::size_t i = 0; i < plan.edits.size(); ++i) {
        std::cout << "edit[" << i << "]=" << plan.edits[i].start << ':'
                  << plan.edits[i].length << '\n';
    }
}

std::optional<Arguments> parse_arguments(int argc, char** argv) {
    Arguments arguments;

    if (argc < 2) {
        return std::nullopt;
    }

    const std::string_view command = argv[1];
    if (command == "-h" || command == "--help") {
        arguments.help = true;
        return arguments;
    }
    if (command == "--version") {
        arguments.version = true;
        return arguments;
    }
    if (command == "scan") {
        arguments.command = Command::Scan;
    } else if (command == "replace") {
        arguments.command = Command::Replace;
    } else if (command == "delete") {
        arguments.command = Command::Delete;
    } else {
        std::cerr << "rangeedit: unknown command '" << command << "'\n";
        return std::nullopt;
    }

    for (int i = 2; i < argc; ++i) {
        const std::string_view argument = argv[i];

        if (argument == "-h" || argument == "--help") {
            arguments.help = true;
        } else if (argument == "--version") {
            arguments.version = true;
        } else if (argument == "--ignore-case") {
            arguments.match_options.case_sensitive = false;
        } else if (argument == "--whole-word") {
            arguments.match_options.whole_word = true;
        } else if (argument == "--regex") {
            arguments.regex = true;
        } else if (argument == "--dry-run") {
            arguments.dry_run = true;
        } else if (argument == "--json") {
            arguments.json = true;
        } else if (argument == "--pattern") {
            if (i + 1 >= argc) {
                std::cerr << "rangeedit: --pattern requires a value\n";
                return std::nullopt;
            }
            arguments.pattern = argv[++i];
            arguments.has_pattern = true;
        } else if (argument == "--replacement") {
            if (i + 1 >= argc) {
                std::cerr << "rangeedit: --replacement requires a value\n";
                return std::nullopt;
            }
            arguments.replacement = argv[++i];
            arguments.has_replacement = true;
        } else if (argument == "--output") {
            if (i + 1 >= argc) {
                std::cerr << "rangeedit: --output requires a value\n";
                return std::nullopt;
            }
            arguments.output = argv[++i];
        } else if (argument == "--max") {
            if (i + 1 >= argc) {
                std::cerr << "rangeedit: --max requires a value\n";
                return std::nullopt;
            }
            const auto value = parse_size(argv[++i]);
            if (!value.has_value()) {
                std::cerr << "rangeedit: invalid --max value\n";
                return std::nullopt;
            }
            arguments.match_options.max_matches = *value;
        } else if (argument == "--algorithm") {
            if (i + 1 >= argc) {
                std::cerr << "rangeedit: --algorithm requires a value\n";
                return std::nullopt;
            }
            const auto value = parse_algorithm(argv[++i]);
            if (!value.has_value()) {
                std::cerr << "rangeedit: invalid --algorithm value\n";
                return std::nullopt;
            }
            arguments.algorithm = *value;
        } else if (!argument.empty() && argument.front() == '-') {
            std::cerr << "rangeedit: unknown option '" << argument << "'\n";
            return std::nullopt;
        } else if (arguments.file.empty()) {
            arguments.file = argument;
        } else {
            std::cerr << "rangeedit: unexpected argument '" << argument << "'\n";
            return std::nullopt;
        }
    }

    if (arguments.help || arguments.version) {
        return arguments;
    }

    if (!arguments.has_pattern) {
        std::cerr << "rangeedit: --pattern is required\n";
        return std::nullopt;
    }

    if (arguments.file.empty()) {
        std::cerr << "rangeedit: FILE is required\n";
        return std::nullopt;
    }

    if (arguments.command == Command::Scan) {
        if (arguments.has_replacement) {
            std::cerr << "rangeedit: --replacement is only valid for replace\n";
            return std::nullopt;
        }
        if (arguments.dry_run) {
            std::cerr << "rangeedit: --dry-run is only valid for replace or delete\n";
            return std::nullopt;
        }
        if (!arguments.output.empty()) {
            std::cerr << "rangeedit: --output is only valid for replace or delete\n";
            return std::nullopt;
        }
    } else if (arguments.command == Command::Replace) {
        if (!arguments.has_replacement) {
            std::cerr << "rangeedit: --replacement is required for replace\n";
            return std::nullopt;
        }
    } else if (arguments.has_replacement) {
        std::cerr << "rangeedit: --replacement is only valid for replace\n";
        return std::nullopt;
    }

    if (arguments.dry_run && !arguments.output.empty()) {
        std::cerr << "rangeedit: --output cannot be combined with --dry-run\n";
        return std::nullopt;
    }

    if (arguments.json && arguments.dry_run == false && arguments.command != Command::Scan) {
        std::cerr << "rangeedit: --json for replace/delete requires --dry-run\n";
        return std::nullopt;
    }

    return arguments;
}

bool find_matches(
    const Arguments& arguments,
    std::string_view text,
    rangeedit::MatchList& matches,
    std::string& error) {
    if (arguments.regex) {
        auto result = rangeedit::find_regex(
            text,
            std::string_view(arguments.pattern),
            arguments.match_options);
        if (!result.ok()) {
            error = std::move(result.error);
            return false;
        }
        matches = std::move(result.matches);
        return true;
    }

    matches = rangeedit::find_literal(
        text,
        std::string_view(arguments.pattern),
        arguments.match_options,
        arguments.algorithm);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const auto arguments = parse_arguments(argc, argv);
    if (!arguments.has_value()) {
        print_usage(std::cerr);
        return 2;
    }

    if (arguments->help) {
        print_usage(std::cout);
        return 0;
    }

    if (arguments->version) {
        std::cout << kVersion << '\n';
        return 0;
    }

    if (arguments->command == Command::Scan && !arguments->regex) {
        if (!scan_literal_stream(*arguments)) {
            std::cerr << "rangeedit: could not read '" << arguments->file << "'\n";
            return 2;
        }
        return 0;
    }

    const auto text = read_file(arguments->file);
    if (!text.has_value()) {
        std::cerr << "rangeedit: could not read '" << arguments->file << "'\n";
        return 2;
    }

    rangeedit::MatchList matches;
    std::string match_error;
    if (!find_matches(*arguments, std::string_view(*text), matches, match_error)) {
        std::cerr << "rangeedit: invalid regular expression: " << match_error << '\n';
        return 2;
    }

    if (arguments->command == Command::Scan) {
        print_matches(arguments->file, std::string_view(*text), matches, arguments->json);
        std::cerr << matches.size() << " match(es)\n";
        return 0;
    }

    const auto mode = arguments->command == Command::Delete
                          ? rangeedit::EditMode::Delete
                          : rangeedit::EditMode::Replace;

    const auto plan = rangeedit::plan_edits(
        std::string_view(*text),
        matches,
        std::string_view(arguments->replacement),
        mode);

    if (!plan.ok()) {
        std::cerr << "rangeedit: " << plan.error << '\n';
        return 2;
    }

    if (arguments->dry_run) {
        print_plan(plan, arguments->json);
        return 0;
    }

    const auto replacement = mode == rangeedit::EditMode::Delete
                                 ? std::string_view{}
                                 : std::string_view(arguments->replacement);

    const auto result = rangeedit::apply_edits(
        std::string_view(*text),
        plan.edits,
        replacement);

    if (!result.ok()) {
        std::cerr << "rangeedit: " << result.error << '\n';
        return 2;
    }

    if (!arguments->output.empty()) {
        if (!write_file_atomic(arguments->output, result.text)) {
            std::cerr << "rangeedit: could not write '" << arguments->output << "'\n";
            return 2;
        }
    } else {
        std::cout.write(result.text.data(), static_cast<std::streamsize>(result.text.size()));
    }

    std::cerr << plan.stats.edit_count << " edit(s)\n";
    return 0;
}
