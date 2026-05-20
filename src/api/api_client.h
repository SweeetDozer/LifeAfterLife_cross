#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace api {

struct JsonValue;
using JsonObject = std::map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;

struct JsonValue {
    using Variant = std::variant<std::nullptr_t, bool, double, std::string, JsonObject, JsonArray>;
    Variant value;
};

std::optional<JsonValue> parse_json(std::string_view json_text);
const JsonObject* as_object(const JsonValue* value);
const JsonArray* as_array(const JsonValue* value);
const JsonValue* json_find_member(const JsonObject* object, const std::string& key);
std::optional<std::string> json_get_string(const JsonObject* object, const std::string& key);
std::optional<double> json_get_number(const JsonObject* object, const std::string& key);
std::optional<int> json_get_int(const JsonObject* object, const std::string& key);
std::optional<bool> json_get_bool(const JsonObject* object, const std::string& key);

enum class ApiErrorType {
    Network,
    Http,
    Parse,
};

struct ApiError {
    ApiErrorType type = ApiErrorType::Network;
    int http_status = 0;
    std::string message;
};

template<typename T>
struct ApiResult {
    bool ok = false;
    T value {};
    std::optional<ApiError> error;

    static ApiResult success(T result)
    {
        return ApiResult {
            .ok = true,
            .value = std::move(result),
            .error = std::nullopt,
        };
    }

    static ApiResult failure(ApiError api_error)
    {
        return ApiResult {
            .ok = false,
            .value = {},
            .error = std::move(api_error),
        };
    }
};

struct HttpResponse {
    int status_code = 0;
    std::string body;
    std::optional<JsonValue> json_body;
};

class ApiClient {
public:
    explicit ApiClient(std::string base_url = "https://api.lal.mors.space");

    void set_access_token(std::string access_token);
    void clear_access_token();
    [[nodiscard]] const std::string& access_token() const;
    [[nodiscard]] const std::string& base_url() const;

    ApiResult<HttpResponse> get_json(const std::string& path) const;
    ApiResult<HttpResponse> post_json(const std::string& path, std::string_view json_body) const;
    ApiResult<HttpResponse> patch_json(const std::string& path, std::string_view json_body) const;
    ApiResult<HttpResponse> delete_json(const std::string& path) const;

private:
    ApiResult<HttpResponse> request_json(const std::wstring& method, const std::string& path, std::string_view json_body) const;

    std::string base_url_;
    std::string access_token_;
};

} // namespace api
