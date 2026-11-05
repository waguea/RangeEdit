#include "rangeedit/edit.hpp"
#include "rangeedit/literal.hpp"
#include "rangeedit/regex.hpp"
#include "rangeedit/types.hpp"

#include <node_api.h>

#include <array>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace {

constexpr napi_type_tag kCancelTokenTag = {0x52616E6765456469ULL, 0x7443616E63656C54ULL};

enum class Operation {
    Scan,
    Edit,
};

struct ProgressData {
    std::size_t processed{};
    std::size_t total{};
    std::mutex mutex;
    std::condition_variable condition;
    bool done = false;
};

struct ParsedOptions {
    bool regex = false;
    bool case_sensitive = true;
    bool whole_word = false;
    bool dry_run = false;
    std::size_t max_matches = std::numeric_limits<std::size_t>::max();
    rangeedit::LiteralAlgorithm algorithm = rangeedit::LiteralAlgorithm::Bmh;
    napi_value cancel_token_value = nullptr;
    rangeedit::CancellationToken* cancel_token = nullptr;
    napi_value progress_value = nullptr;
};

struct WorkerData {
    Operation operation = Operation::Scan;
    bool delete_mode = false;
    bool regex = false;
    bool case_sensitive = true;
    bool whole_word = false;
    bool dry_run = false;
    std::size_t max_matches = std::numeric_limits<std::size_t>::max();
    rangeedit::LiteralAlgorithm algorithm = rangeedit::LiteralAlgorithm::Bmh;
    std::u16string text;
    std::u16string pattern;
    std::u16string replacement;
    rangeedit::MatchOptions match_options;
    rangeedit::MatchList matches;
    rangeedit::EditPlan plan;
    rangeedit::Apply16Result applied;
    std::string error;
    bool cancelled = false;
    std::size_t processed = 0;
    rangeedit::CancellationToken* token = nullptr;
    napi_ref token_ref = nullptr;
    napi_threadsafe_function progress_tsfn = nullptr;
    napi_deferred deferred = nullptr;
    napi_async_work work = nullptr;
};

bool ThrowTypeError(napi_env env, const std::string& message) {
    napi_throw_type_error(env, nullptr, message.c_str());
    return false;
}

bool ThrowError(napi_env env, const std::string& message) {
    napi_throw_error(env, nullptr, message.c_str());
    return false;
}

napi_value Undefined(napi_env env) {
    napi_value value = nullptr;
    napi_get_undefined(env, &value);
    return value;
}

bool GetString(napi_env env, napi_value value, std::u16string& output, std::string& error) {
    napi_valuetype type = napi_undefined;
    if (napi_typeof(env, value, &type) != napi_ok || type != napi_string) {
        error = "expected a string argument";
        return false;
    }

    std::size_t length = 0;
    if (napi_get_value_string_utf16(env, value, nullptr, 0, &length) != napi_ok) {
        error = "could not read string length";
        return false;
    }

    std::u16string buffer(length + 1, u'\0');
    std::size_t copied = 0;
    if (napi_get_value_string_utf16(
            env, value, buffer.data(), buffer.size(), &copied) != napi_ok) {
        error = "could not read string value";
        return false;
    }

    buffer.resize(copied);
    output = std::move(buffer);
    return true;
}

bool GetUtf8String(napi_env env, napi_value value, std::string& output, std::string& error) {
    napi_valuetype type = napi_undefined;
    if (napi_typeof(env, value, &type) != napi_ok || type != napi_string) {
        error = "expected a string argument";
        return false;
    }

    std::size_t length = 0;
    if (napi_get_value_string_utf8(env, value, nullptr, 0, &length) != napi_ok) {
        error = "could not read string length";
        return false;
    }

    std::string buffer(length + 1, '\0');
    std::size_t copied = 0;
    if (napi_get_value_string_utf8(
            env, value, buffer.data(), buffer.size(), &copied) != napi_ok) {
        error = "could not read string value";
        return false;
    }

    buffer.resize(copied);
    output = std::move(buffer);
    return true;
}

bool GetProperty(
    napi_env env,
    napi_value object,
    const char* name,
    napi_value& value,
    bool& present) {
    if (napi_has_named_property(env, object, name, &present) != napi_ok) {
        return false;
    }
    if (!present) {
        return true;
    }
    return napi_get_named_property(env, object, name, &value) == napi_ok;
}

bool GetBoolOption(
    napi_env env,
    napi_value options,
    const char* name,
    bool& value,
    std::string& error) {
    napi_value property = nullptr;
    bool present = false;
    if (!GetProperty(env, options, name, property, present)) {
        error = std::string("could not read option ") + name;
        return false;
    }
    if (!present) {
        return true;
    }

    napi_valuetype type = napi_undefined;
    if (napi_typeof(env, property, &type) != napi_ok || type != napi_boolean) {
        error = std::string("option ") + name + " must be a boolean";
        return false;
    }

    return napi_get_value_bool(env, property, &value) == napi_ok;
}

bool GetMaxMatches(
    napi_env env,
    napi_value options,
    std::size_t& value,
    std::string& error) {
    napi_value property = nullptr;
    bool present = false;
    if (!GetProperty(env, options, "maxMatches", property, present)) {
        error = "could not read option maxMatches";
        return false;
    }
    if (!present) {
        return true;
    }

    napi_valuetype type = napi_undefined;
    if (napi_typeof(env, property, &type) != napi_ok || type != napi_number) {
        error = "option maxMatches must be a non-negative integer";
        return false;
    }

    double number = 0.0;
    if (napi_get_value_double(env, property, &number) != napi_ok ||
        !std::isfinite(number) || number < 0.0 || std::floor(number) != number ||
        number > 9007199254740991.0) {
        error = "option maxMatches must be a safe non-negative integer";
        return false;
    }

    if (number >= static_cast<double>(std::numeric_limits<std::size_t>::max())) {
        value = std::numeric_limits<std::size_t>::max();
    } else {
        value = static_cast<std::size_t>(number);
    }
    return true;
}

bool GetAlgorithm(
    napi_env env,
    napi_value options,
    rangeedit::LiteralAlgorithm& value,
    std::string& error) {
    napi_value property = nullptr;
    bool present = false;
    if (!GetProperty(env, options, "algorithm", property, present)) {
        error = "could not read option algorithm";
        return false;
    }
    if (!present) {
        return true;
    }

    std::string name;
    if (!GetUtf8String(env, property, name, error)) {
        return false;
    }

    if (name == "naive") {
        value = rangeedit::LiteralAlgorithm::Naive;
    } else if (name == "bmh") {
        value = rangeedit::LiteralAlgorithm::Bmh;
    } else if (name == "kmp") {
        value = rangeedit::LiteralAlgorithm::Kmp;
    } else if (name == "std") {
        value = rangeedit::LiteralAlgorithm::StdSearch;
    } else {
        error = "option algorithm must be one of naive, bmh, kmp, std";
        return false;
    }
    return true;
}

bool GetCancelToken(
    napi_env env,
    napi_value options,
    ParsedOptions& parsed,
    std::string& error) {
    napi_value property = nullptr;
    bool present = false;
    if (!GetProperty(env, options, "cancelToken", property, present)) {
        error = "could not read option cancelToken";
        return false;
    }
    if (!present) {
        return true;
    }

    napi_valuetype type = napi_undefined;
    if (napi_typeof(env, property, &type) != napi_ok || type != napi_object) {
        error = "option cancelToken must be created by createCancelToken()";
        return false;
    }

    bool matches = false;
    if (napi_check_object_type_tag(env, property, &kCancelTokenTag, &matches) != napi_ok ||
        !matches) {
        error = "option cancelToken must be created by createCancelToken()";
        return false;
    }

    void* token = nullptr;
    if (napi_unwrap(env, property, &token) != napi_ok || token == nullptr) {
        error = "could not access cancel token";
        return false;
    }

    parsed.cancel_token_value = property;
    parsed.cancel_token = static_cast<rangeedit::CancellationToken*>(token);
    return true;
}

bool GetProgressCallback(
    napi_env env,
    napi_value options,
    ParsedOptions& parsed,
    std::string& error) {
    napi_value property = nullptr;
    bool present = false;
    if (!GetProperty(env, options, "onProgress", property, present)) {
        error = "could not read option onProgress";
        return false;
    }
    if (!present) {
        return true;
    }

    napi_valuetype type = napi_undefined;
    if (napi_typeof(env, property, &type) != napi_ok || type != napi_function) {
        error = "option onProgress must be a function";
        return false;
    }

    parsed.progress_value = property;
    return true;
}

bool ParseOptions(
    napi_env env,
    napi_value value,
    ParsedOptions& parsed,
    std::string& error) {
    if (value == nullptr) {
        return true;
    }

    napi_valuetype type = napi_undefined;
    if (napi_typeof(env, value, &type) != napi_ok) {
        error = "could not read options";
        return false;
    }
    if (type == napi_undefined || type == napi_null) {
        return true;
    }
    if (type != napi_object) {
        error = "options must be an object";
        return false;
    }

    if (!GetBoolOption(env, value, "regex", parsed.regex, error) ||
        !GetBoolOption(env, value, "caseSensitive", parsed.case_sensitive, error) ||
        !GetBoolOption(env, value, "wholeWord", parsed.whole_word, error) ||
        !GetBoolOption(env, value, "dryRun", parsed.dry_run, error) ||
        !GetMaxMatches(env, value, parsed.max_matches, error) ||
        !GetAlgorithm(env, value, parsed.algorithm, error) ||
        !GetCancelToken(env, value, parsed, error) ||
        !GetProgressCallback(env, value, parsed, error)) {
        return false;
    }

    return true;
}

void FinalizeCancelToken(napi_env /*env*/, void* data, void* /*hint*/) {
    delete static_cast<rangeedit::CancellationToken*>(data);
}

napi_value CancelTokenCancel(napi_env env, napi_callback_info info) {
    napi_value self = nullptr;
    std::size_t argc = 0;
    if (napi_get_cb_info(env, info, &argc, nullptr, &self, nullptr) != napi_ok ||
        self == nullptr) {
        ThrowTypeError(env, "cancel() must be called on a cancel token");
        return nullptr;
    }

    bool matches = false;
    if (napi_check_object_type_tag(env, self, &kCancelTokenTag, &matches) != napi_ok ||
        !matches) {
        ThrowTypeError(env, "cancel() must be called on a cancel token");
        return nullptr;
    }

    void* token = nullptr;
    if (napi_unwrap(env, self, &token) != napi_ok || token == nullptr) {
        ThrowError(env, "cancel token is no longer available");
        return nullptr;
    }

    static_cast<rangeedit::CancellationToken*>(token)->cancel();
    return Undefined(env);
}

napi_value CreateCancelToken(napi_env env, napi_callback_info /*info*/) {
    auto* token = new (std::nothrow) rangeedit::CancellationToken();
    if (token == nullptr) {
        ThrowError(env, "out of memory");
        return nullptr;
    }

    napi_value object = nullptr;
    if (napi_create_object(env, &object) != napi_ok ||
        napi_wrap(env, object, token, FinalizeCancelToken, nullptr, nullptr) != napi_ok) {
        delete token;
        ThrowError(env, "could not create cancel token");
        return nullptr;
    }

    if (napi_type_tag_object(env, object, &kCancelTokenTag) != napi_ok) {
        ThrowError(env, "could not tag cancel token");
        return nullptr;
    }

    napi_value cancel = nullptr;
    if (napi_create_function(
            env, "cancel", NAPI_AUTO_LENGTH, CancelTokenCancel, nullptr, &cancel) != napi_ok ||
        napi_set_named_property(env, object, "cancel", cancel) != napi_ok) {
        ThrowError(env, "could not create cancel()");
        return nullptr;
    }

    return object;
}

void PostProgress(WorkerData* worker, std::size_t processed) {
    if (worker->progress_tsfn == nullptr) {
        return;
    }

    ProgressData progress;
    progress.processed = processed;
    progress.total = worker->text.size();
    std::unique_lock lock(progress.mutex);
    if (napi_call_threadsafe_function(
            worker->progress_tsfn, &progress, napi_tsfn_blocking) != napi_ok) {
        return;
    }

    progress.condition.wait(lock, [&progress] { return progress.done; });
}

void CallProgress(
    napi_env env,
    napi_value js_callback,
    void* /*context*/,
    void* data) {
    auto* progress = static_cast<ProgressData*>(data);
    if (env != nullptr && js_callback != nullptr) {
        napi_value object = nullptr;
        napi_value processed = nullptr;
        napi_value total = nullptr;
        napi_value result = nullptr;

        if (napi_create_object(env, &object) == napi_ok &&
            napi_create_double(env, static_cast<double>(progress->processed), &processed) ==
                napi_ok &&
            napi_create_double(env, static_cast<double>(progress->total), &total) == napi_ok &&
            napi_set_named_property(env, object, "processed", processed) == napi_ok &&
            napi_set_named_property(env, object, "total", total) == napi_ok) {
            const napi_status status = napi_call_function(
                env, Undefined(env), js_callback, 1, &object, &result);
            if (status == napi_pending_exception) {
                napi_value ignored = nullptr;
                napi_get_and_clear_last_exception(env, &ignored);
            }
        }
    }

    {
        const std::lock_guard lock(progress->mutex);
        progress->done = true;
    }
    progress->condition.notify_one();
}

void Execute(napi_env /*env*/, void* data) {
    auto* worker = static_cast<WorkerData*>(data);

    try {
        worker->match_options.case_sensitive = worker->case_sensitive;
        worker->match_options.whole_word = worker->whole_word;
        worker->match_options.max_matches = worker->max_matches;
        worker->match_options.cancellation_token = worker->token;
        if (worker->progress_tsfn != nullptr) {
            worker->match_options.progress_callback = [worker](std::size_t processed) {
                worker->processed = processed;
                PostProgress(worker, processed);
            };
        }

        PostProgress(worker, 0);

        if (worker->regex) {
            auto result = rangeedit::find_regex(
                std::u16string_view(worker->text),
                std::u16string_view(worker->pattern),
                worker->match_options);
            if (!result.ok()) {
                worker->error = std::move(result.error);
                return;
            }
            worker->matches = std::move(result.matches);
        } else {
            worker->matches = rangeedit::find_literal(
                std::u16string_view(worker->text),
                std::u16string_view(worker->pattern),
                worker->match_options,
                worker->algorithm);
        }

        if (worker->token != nullptr && worker->token->cancelled()) {
            worker->cancelled = true;
            return;
        }

        worker->processed = worker->text.size();

        if (worker->operation == Operation::Edit) {
            const auto input = std::u16string_view(worker->text);
            if (worker->delete_mode) {
                worker->plan = rangeedit::plan_delete(input, worker->matches);
            } else {
                worker->plan = rangeedit::plan_edits(
                    input,
                    worker->matches,
                    std::u16string_view(worker->replacement),
                    rangeedit::EditMode::Replace);
            }

            if (!worker->plan.ok()) {
                worker->error = worker->plan.error;
                return;
            }

            if (!worker->dry_run) {
                const auto replacement = worker->delete_mode
                                             ? std::u16string_view{}
                                             : std::u16string_view(worker->replacement);
                worker->applied = rangeedit::apply_edits(input, worker->plan.edits, replacement);
                if (!worker->applied.ok()) {
                    worker->error = worker->applied.error;
                    return;
                }
            }
        }

        PostProgress(worker, worker->processed);
    } catch (const std::exception& exception) {
        worker->error = exception.what();
    } catch (...) {
        worker->error = "unknown native error";
    }
}

bool SetNumber(napi_env env, napi_value object, const char* name, double value) {
    napi_value number = nullptr;
    return napi_create_double(env, value, &number) == napi_ok &&
           napi_set_named_property(env, object, name, number) == napi_ok;
}

bool SetSize(napi_env env, napi_value object, const char* name, std::size_t value) {
    return SetNumber(env, object, name, static_cast<double>(value));
}

bool SetBool(napi_env env, napi_value object, const char* name, bool value) {
    napi_value boolean = nullptr;
    return napi_get_boolean(env, value, &boolean) == napi_ok &&
           napi_set_named_property(env, object, name, boolean) == napi_ok;
}

bool SetString(
    napi_env env,
    napi_value object,
    const char* name,
    std::u16string_view value) {
    napi_value string = nullptr;
    return napi_create_string_utf16(env, value.data(), value.size(), &string) == napi_ok &&
           napi_set_named_property(env, object, name, string) == napi_ok;
}

bool SetMatches(napi_env env, napi_value object, const rangeedit::MatchList& matches) {
    if (matches.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    napi_value array = nullptr;
    if (napi_create_array_with_length(env, matches.size(), &array) != napi_ok) {
        return false;
    }

    for (std::size_t i = 0; i < matches.size(); ++i) {
        napi_value entry = nullptr;
        if (napi_create_object(env, &entry) != napi_ok ||
            !SetSize(env, entry, "start", matches[i].start) ||
            !SetSize(env, entry, "length", matches[i].length) ||
            napi_set_element(env, array, static_cast<std::uint32_t>(i), entry) != napi_ok) {
            return false;
        }
    }

    return napi_set_named_property(env, object, "matches", array) == napi_ok;
}

bool SetEdits(napi_env env, napi_value object, const rangeedit::EditList& edits) {
    if (edits.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    napi_value array = nullptr;
    if (napi_create_array_with_length(env, edits.size(), &array) != napi_ok) {
        return false;
    }

    for (std::size_t i = 0; i < edits.size(); ++i) {
        napi_value entry = nullptr;
        if (napi_create_object(env, &entry) != napi_ok ||
            !SetSize(env, entry, "start", edits[i].start) ||
            !SetSize(env, entry, "length", edits[i].length) ||
            napi_set_element(env, array, static_cast<std::uint32_t>(i), entry) != napi_ok) {
            return false;
        }
    }

    return napi_set_named_property(env, object, "edits", array) == napi_ok;
}

bool SetStats(napi_env env, napi_value object, const rangeedit::EditPlanStats& stats) {
    napi_value result = nullptr;
    return napi_create_object(env, &result) == napi_ok &&
           SetSize(env, result, "editCount", stats.edit_count) &&
           SetSize(env, result, "removedUnits", stats.removed_units) &&
           SetSize(env, result, "insertedUnits", stats.inserted_units) &&
           SetSize(env, result, "outputUnits", stats.output_units) &&
           napi_set_named_property(env, object, "stats", result) == napi_ok;
}

bool BuildResult(napi_env env, const WorkerData& worker, napi_value& result) {
    if (napi_create_object(env, &result) != napi_ok ||
        !SetMatches(env, result, worker.matches) ||
        !SetBool(env, result, "cancelled", worker.cancelled) ||
        !SetSize(env, result, "processed", worker.processed)) {
        return false;
    }

    if (worker.operation == Operation::Edit) {
        if (!SetEdits(env, result, worker.plan.edits) ||
            !SetStats(env, result, worker.plan.stats)) {
            return false;
        }

        if (!worker.dry_run && worker.applied.ok()) {
            if (!SetString(env, result, "text", std::u16string_view(worker.applied.text))) {
                return false;
            }
        }
    }

    return true;
}

void RejectDeferred(napi_env env, napi_deferred deferred, const std::string& message) {
    napi_value message_value = nullptr;
    napi_value error = nullptr;
    if (napi_create_string_utf8(env, message.c_str(), NAPI_AUTO_LENGTH, &message_value) ==
            napi_ok &&
        napi_create_error(env, nullptr, message_value, &error) == napi_ok) {
        napi_reject_deferred(env, deferred, error);
        return;
    }

    napi_reject_deferred(env, deferred, Undefined(env));
}

void CleanupHandles(napi_env env, WorkerData* worker) {
    if (worker->progress_tsfn != nullptr) {
        napi_release_threadsafe_function(worker->progress_tsfn, napi_tsfn_release);
        worker->progress_tsfn = nullptr;
    }
    if (worker->token_ref != nullptr) {
        napi_delete_reference(env, worker->token_ref);
        worker->token_ref = nullptr;
    }
    if (worker->work != nullptr) {
        napi_delete_async_work(env, worker->work);
        worker->work = nullptr;
    }
}

void Complete(napi_env env, napi_status status, void* data) {
    auto* worker = static_cast<WorkerData*>(data);
    if (status != napi_ok && worker->error.empty()) {
        worker->error = "native async work failed";
    }

    if (worker->error.empty()) {
        napi_value result = nullptr;
        if (BuildResult(env, *worker, result)) {
            napi_resolve_deferred(env, worker->deferred, result);
        } else {
            worker->error = "could not build native result";
        }
    }

    if (!worker->error.empty()) {
        RejectDeferred(env, worker->deferred, worker->error);
    }

    CleanupHandles(env, worker);
    delete worker;
}

napi_value StartOperation(
    napi_env env,
    napi_callback_info info,
    Operation operation,
    bool delete_mode) {
    const std::size_t required_arguments =
        operation == Operation::Scan ? 2 : (delete_mode ? 2 : 3);

    std::array<napi_value, 4> arguments{};
    std::size_t argument_count = arguments.size();
    if (napi_get_cb_info(
            env, info, &argument_count, arguments.data(), nullptr, nullptr) != napi_ok) {
        ThrowError(env, "could not read arguments");
        return nullptr;
    }

    if (argument_count < required_arguments) {
        ThrowTypeError(
            env,
            operation == Operation::Scan
                ? "scan(text, pattern, options?) requires text and pattern"
                : "edit operation is missing required arguments");
        return nullptr;
    }

    auto* worker = new (std::nothrow) WorkerData();
    if (worker == nullptr) {
        ThrowError(env, "out of memory");
        return nullptr;
    }

    worker->operation = operation;
    worker->delete_mode = delete_mode;

    std::string error;
    if (!GetString(env, arguments[0], worker->text, error) ||
        !GetString(env, arguments[1], worker->pattern, error)) {
        delete worker;
        ThrowTypeError(env, error);
        return nullptr;
    }

    if (operation == Operation::Edit && !delete_mode) {
        if (!GetString(env, arguments[2], worker->replacement, error)) {
            delete worker;
            ThrowTypeError(env, error);
            return nullptr;
        }
    }

    ParsedOptions parsed;
    const napi_value options_value = argument_count > required_arguments
                                         ? arguments[required_arguments]
                                         : nullptr;
    if (!ParseOptions(env, options_value, parsed, error)) {
        delete worker;
        ThrowTypeError(env, error);
        return nullptr;
    }

    if (operation == Operation::Scan && parsed.dry_run) {
        delete worker;
        ThrowTypeError(env, "dryRun is only valid for replace/delete");
        return nullptr;
    }

    worker->regex = parsed.regex;
    worker->case_sensitive = parsed.case_sensitive;
    worker->whole_word = parsed.whole_word;
    worker->dry_run = parsed.dry_run;
    worker->max_matches = parsed.max_matches;
    worker->algorithm = parsed.algorithm;
    worker->token = parsed.cancel_token;

    if (parsed.cancel_token_value != nullptr) {
        if (napi_create_reference(env, parsed.cancel_token_value, 1, &worker->token_ref) !=
            napi_ok) {
            CleanupHandles(env, worker);
            delete worker;
            ThrowError(env, "could not retain cancel token");
            return nullptr;
        }
    }

    if (parsed.progress_value != nullptr) {
        napi_value resource_name = nullptr;
        if (napi_create_string_utf8(
                env, "RangeEditProgress", NAPI_AUTO_LENGTH, &resource_name) != napi_ok ||
            napi_create_threadsafe_function(
                env,
                parsed.progress_value,
                options_value,
                resource_name,
                0,
                1,
                nullptr,
                nullptr,
                nullptr,
                CallProgress,
                &worker->progress_tsfn) != napi_ok) {
            CleanupHandles(env, worker);
            delete worker;
            ThrowError(env, "could not create progress callback");
            return nullptr;
        }
    }

    napi_value promise = nullptr;
    if (napi_create_promise(env, &worker->deferred, &promise) != napi_ok) {
        CleanupHandles(env, worker);
        delete worker;
        ThrowError(env, "could not create promise");
        return nullptr;
    }

    napi_value resource_name = nullptr;
    const char* resource_text = operation == Operation::Scan ? "RangeEditScan" : "RangeEditEdit";
    if (napi_create_string_utf8(env, resource_text, NAPI_AUTO_LENGTH, &resource_name) !=
            napi_ok ||
        napi_create_async_work(
            env,
            options_value,
            resource_name,
            Execute,
            Complete,
            worker,
            &worker->work) != napi_ok) {
        RejectDeferred(env, worker->deferred, "could not create async work");
        CleanupHandles(env, worker);
        delete worker;
        return nullptr;
    }

    if (napi_queue_async_work(env, worker->work) != napi_ok) {
        RejectDeferred(env, worker->deferred, "could not queue async work");
        CleanupHandles(env, worker);
        delete worker;
        return nullptr;
    }

    return promise;
}

napi_value Scan(napi_env env, napi_callback_info info) {
    return StartOperation(env, info, Operation::Scan, false);
}

napi_value Replace(napi_env env, napi_callback_info info) {
    return StartOperation(env, info, Operation::Edit, false);
}

napi_value Delete(napi_env env, napi_callback_info info) {
    return StartOperation(env, info, Operation::Edit, true);
}

bool ExportFunction(
    napi_env env,
    napi_value exports,
    const char* name,
    napi_callback callback) {
    napi_value function = nullptr;
    return napi_create_function(env, name, NAPI_AUTO_LENGTH, callback, nullptr, &function) ==
               napi_ok &&
           napi_set_named_property(env, exports, name, function) == napi_ok;
}

}  // namespace

NAPI_MODULE_INIT() {
    if (!ExportFunction(env, exports, "createCancelToken", CreateCancelToken) ||
        !ExportFunction(env, exports, "scan", Scan) ||
        !ExportFunction(env, exports, "replace", Replace) ||
        !ExportFunction(env, exports, "delete", Delete)) {
        return nullptr;
    }

    napi_value version = nullptr;
    if (napi_create_string_utf8(env, "0.1.0", NAPI_AUTO_LENGTH, &version) == napi_ok) {
        napi_set_named_property(env, exports, "version", version);
    }

    return exports;
}
