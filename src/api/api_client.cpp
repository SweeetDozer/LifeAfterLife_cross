#include "api_client.h"

#include <charconv>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string_view>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#endif

namespace api {

namespace {

class JsonParser {
public:
    explicit JsonParser(std::string_view source) : source_(source) {}

    std::optional<JsonValue> parse()
    {
        skip_whitespace();
        auto value = parse_value();
        if (!value) {
            return std::nullopt;
        }
        skip_whitespace();
        if (position_ != source_.size()) {
            return std::nullopt;
        }
        return value;
    }

private:
    std::optional<JsonValue> parse_value()
    {
        skip_whitespace();
        if (position_ >= source_.size()) {
            return std::nullopt;
        }

        const char current = source_[position_];
        if (current == '{') {
            return parse_object();
        }
        if (current == '[') {
            return parse_array();
        }
        if (current == '"') {
            auto text = parse_string();
            if (!text) {
                return std::nullopt;
            }
            return JsonValue { *text };
        }
        if (current == 't') {
            return parse_literal("true", JsonValue { true });
        }
        if (current == 'f') {
            return parse_literal("false", JsonValue { false });
        }
        if (current == 'n') {
            return parse_literal("null", JsonValue { nullptr });
        }
        if (current == '-' || std::isdigit(static_cast<unsigned char>(current))) {
            auto number = parse_number();
            if (!number) {
                return std::nullopt;
            }
            return JsonValue { *number };
        }

        return std::nullopt;
    }

    std::optional<JsonValue> parse_object()
    {
        if (!consume('{')) {
            return std::nullopt;
        }

        JsonObject object;
        skip_whitespace();
        if (consume('}')) {
            return JsonValue { object };
        }

        while (position_ < source_.size()) {
            auto key = parse_string();
            if (!key) {
                return std::nullopt;
            }

            skip_whitespace();
            if (!consume(':')) {
                return std::nullopt;
            }

            auto value = parse_value();
            if (!value) {
                return std::nullopt;
            }
            object.emplace(*key, *value);

            skip_whitespace();
            if (consume('}')) {
                return JsonValue { object };
            }
            if (!consume(',')) {
                return std::nullopt;
            }
            skip_whitespace();
        }

        return std::nullopt;
    }

    std::optional<JsonValue> parse_array()
    {
        if (!consume('[')) {
            return std::nullopt;
        }

        JsonArray array;
        skip_whitespace();
        if (consume(']')) {
            return JsonValue { array };
        }

        while (position_ < source_.size()) {
            auto value = parse_value();
            if (!value) {
                return std::nullopt;
            }
            array.push_back(*value);

            skip_whitespace();
            if (consume(']')) {
                return JsonValue { array };
            }
            if (!consume(',')) {
                return std::nullopt;
            }
            skip_whitespace();
        }

        return std::nullopt;
    }

    std::optional<std::string> parse_string()
    {
        if (!consume('"')) {
            return std::nullopt;
        }

        std::string output;
        while (position_ < source_.size()) {
            const char current = source_[position_++];
            if (current == '"') {
                return output;
            }
            if (current == '\\') {
                if (position_ >= source_.size()) {
                    return std::nullopt;
                }
                const char escaped = source_[position_++];
                switch (escaped) {
                case '"': output.push_back('"'); break;
                case '\\': output.push_back('\\'); break;
                case '/': output.push_back('/'); break;
                case 'b': output.push_back('\b'); break;
                case 'f': output.push_back('\f'); break;
                case 'n': output.push_back('\n'); break;
                case 'r': output.push_back('\r'); break;
                case 't': output.push_back('\t'); break;
                default: return std::nullopt;
                }
                continue;
            }
            output.push_back(current);
        }

        return std::nullopt;
    }

    std::optional<double> parse_number()
    {
        const std::size_t start = position_;
        if (source_[position_] == '-') {
            ++position_;
        }

        if (position_ >= source_.size()) {
            return std::nullopt;
        }

        if (source_[position_] == '0') {
            ++position_;
        } else {
            if (!std::isdigit(static_cast<unsigned char>(source_[position_]))) {
                return std::nullopt;
            }
            while (position_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[position_]))) {
                ++position_;
            }
        }

        if (position_ < source_.size() && source_[position_] == '.') {
            ++position_;
            while (position_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[position_]))) {
                ++position_;
            }
        }

        if (position_ < source_.size() && (source_[position_] == 'e' || source_[position_] == 'E')) {
            ++position_;
            if (position_ < source_.size() && (source_[position_] == '+' || source_[position_] == '-')) {
                ++position_;
            }
            while (position_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[position_]))) {
                ++position_;
            }
        }

        double parsed_value = 0.0;
        const std::string_view token = source_.substr(start, position_ - start);
        const auto [ptr, error] = std::from_chars(token.data(), token.data() + token.size(), parsed_value);
        if (error == std::errc() && ptr == token.data() + token.size()) {
            return parsed_value;
        }
        return std::nullopt;
    }

    std::optional<JsonValue> parse_literal(std::string_view literal, JsonValue value)
    {
        if (source_.substr(position_, literal.size()) != literal) {
            return std::nullopt;
        }
        position_ += literal.size();
        return value;
    }

    void skip_whitespace()
    {
        while (position_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[position_]))) {
            ++position_;
        }
    }

    bool consume(char expected)
    {
        if (position_ < source_.size() && source_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    std::string_view source_;
    std::size_t position_ = 0;
};

std::wstring utf8_to_wide(std::string_view text)
{
    if (text.empty()) {
        return {};
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::string wide_to_utf8(const std::wstring& text)
{
    if (text.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr);
    return utf8;
}

struct UrlParts {
    bool secure = true;
    std::wstring host;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    std::wstring base_path;
};

std::optional<UrlParts> crack_base_url(const std::string& base_url)
{
    URL_COMPONENTS components {};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);

    std::wstring wide_url = utf8_to_wide(base_url);
    if (!WinHttpCrackUrl(wide_url.c_str(), static_cast<DWORD>(wide_url.size()), 0, &components)) {
        return std::nullopt;
    }

    UrlParts parts;
    parts.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    parts.port = components.nPort;
    parts.host.assign(components.lpszHostName, components.dwHostNameLength);
    parts.base_path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (parts.base_path.empty()) {
        parts.base_path = L"";
    }
    return parts;
}

std::wstring build_request_path(const std::wstring& base_path, const std::string& path)
{
    std::wstring request_path = base_path;
    if (!request_path.empty() && request_path.back() == L'/' && !path.empty() && path.front() == '/') {
        request_path.pop_back();
    }
    request_path += utf8_to_wide(path);
    return request_path.empty() ? L"/" : request_path;
}

ApiError make_network_error(const std::string& message)
{
    return ApiError {
        .type = ApiErrorType::Network,
        .http_status = 0,
        .message = message,
    };
}

ApiError make_http_error(int status_code, std::string message)
{
    return ApiError {
        .type = ApiErrorType::Http,
        .http_status = status_code,
        .message = std::move(message),
    };
}

ApiError make_parse_error(int status_code, std::string message)
{
    return ApiError {
        .type = ApiErrorType::Parse,
        .http_status = status_code,
        .message = std::move(message),
    };
}

} // namespace

std::optional<JsonValue> parse_json(std::string_view json_text)
{
    JsonParser parser(json_text);
    return parser.parse();
}

const JsonObject* as_object(const JsonValue* value)
{
    return value && std::holds_alternative<JsonObject>(value->value)
        ? &std::get<JsonObject>(value->value)
        : nullptr;
}

const JsonArray* as_array(const JsonValue* value)
{
    return value && std::holds_alternative<JsonArray>(value->value)
        ? &std::get<JsonArray>(value->value)
        : nullptr;
}

const JsonValue* json_find_member(const JsonObject* object, const std::string& key)
{
    if (!object) {
        return nullptr;
    }
    const auto it = object->find(key);
    return it != object->end() ? &it->second : nullptr;
}

std::optional<std::string> json_get_string(const JsonObject* object, const std::string& key)
{
    const JsonValue* value = json_find_member(object, key);
    if (value && std::holds_alternative<std::string>(value->value)) {
        return std::get<std::string>(value->value);
    }
    return std::nullopt;
}

std::optional<double> json_get_number(const JsonObject* object, const std::string& key)
{
    const JsonValue* value = json_find_member(object, key);
    if (value && std::holds_alternative<double>(value->value)) {
        return std::get<double>(value->value);
    }
    return std::nullopt;
}

std::optional<int> json_get_int(const JsonObject* object, const std::string& key)
{
    auto number = json_get_number(object, key);
    if (!number) {
        return std::nullopt;
    }
    return static_cast<int>(*number);
}

std::optional<bool> json_get_bool(const JsonObject* object, const std::string& key)
{
    const JsonValue* value = json_find_member(object, key);
    if (value && std::holds_alternative<bool>(value->value)) {
        return std::get<bool>(value->value);
    }
    return std::nullopt;
}

ApiClient::ApiClient(std::string base_url)
    : base_url_(std::move(base_url))
{
}

void ApiClient::set_access_token(std::string access_token)
{
    access_token_ = std::move(access_token);
}

void ApiClient::clear_access_token()
{
    access_token_.clear();
}

const std::string& ApiClient::access_token() const
{
    return access_token_;
}

const std::string& ApiClient::base_url() const
{
    return base_url_;
}

ApiResult<HttpResponse> ApiClient::get_json(const std::string& path) const
{
    return request_json(L"GET", path, "");
}

ApiResult<HttpResponse> ApiClient::post_json(const std::string& path, std::string_view json_body) const
{
    return request_json(L"POST", path, json_body);
}

ApiResult<HttpResponse> ApiClient::patch_json(const std::string& path, std::string_view json_body) const
{
    return request_json(L"PATCH", path, json_body);
}

ApiResult<HttpResponse> ApiClient::delete_json(const std::string& path) const
{
    return request_json(L"DELETE", path, "");
}

ApiResult<HttpResponse> ApiClient::request_json(const std::wstring& method, const std::string& path, std::string_view json_body) const
{
#ifndef _WIN32
    return ApiResult<HttpResponse>::failure(make_network_error("WinHTTP API client is only implemented for Windows."));
#else
    const auto url_parts = crack_base_url(base_url_);
    if (!url_parts) {
        return ApiResult<HttpResponse>::failure(make_network_error("Failed to parse API base URL."));
    }

    HINTERNET session = WinHttpOpen(L"LALDesktop/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS,
                                    0);
    if (!session) {
        return ApiResult<HttpResponse>::failure(make_network_error("Failed to open WinHTTP session."));
    }

    HINTERNET connection = WinHttpConnect(session, url_parts->host.c_str(), url_parts->port, 0);
    if (!connection) {
        WinHttpCloseHandle(session);
        return ApiResult<HttpResponse>::failure(make_network_error("Failed to connect to API host."));
    }

    const std::wstring request_path = build_request_path(url_parts->base_path, path);
    const DWORD request_flags = url_parts->secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connection,
                                           method.c_str(),
                                           request_path.c_str(),
                                           nullptr,
                                           WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           request_flags);
    if (!request) {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return ApiResult<HttpResponse>::failure(make_network_error("Failed to create API request."));
    }

    std::wstring headers = L"Accept: application/json\r\nContent-Type: application/json\r\n";
    if (!access_token_.empty()) {
        headers += L"Authorization: Bearer ";
        headers += utf8_to_wide(access_token_);
        headers += L"\r\n";
    }

    const BOOL send_ok = WinHttpSendRequest(
        request,
        headers.c_str(),
        static_cast<DWORD>(headers.size()),
        json_body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(json_body.data()),
        static_cast<DWORD>(json_body.size()),
        static_cast<DWORD>(json_body.size()),
        0);
    if (!send_ok || !WinHttpReceiveResponse(request, nullptr)) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return ApiResult<HttpResponse>::failure(make_network_error("HTTP request failed."));
    }

    DWORD status_code = 0;
    DWORD status_code_size = sizeof(status_code);
    WinHttpQueryHeaders(request,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX,
                        &status_code,
                        &status_code_size,
                        WINHTTP_NO_HEADER_INDEX);

    std::string response_body;
    for (;;) {
        DWORD available_size = 0;
        if (!WinHttpQueryDataAvailable(request, &available_size)) {
            break;
        }
        if (available_size == 0) {
            break;
        }

        std::string chunk(static_cast<std::size_t>(available_size), '\0');
        DWORD downloaded_size = 0;
        if (!WinHttpReadData(request, chunk.data(), available_size, &downloaded_size)) {
            break;
        }
        chunk.resize(static_cast<std::size_t>(downloaded_size));
        response_body += chunk;
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    HttpResponse response;
    response.status_code = static_cast<int>(status_code);
    response.body = response_body;

    if (!response_body.empty()) {
        response.json_body = parse_json(response_body);
        if (!response.json_body) {
            return ApiResult<HttpResponse>::failure(
                make_parse_error(response.status_code, "Failed to parse JSON response body."));
        }
    }

    if (response.status_code < 200 || response.status_code >= 300) {
        return ApiResult<HttpResponse>::failure(
            make_http_error(response.status_code, response.body.empty() ? "HTTP request failed." : response.body));
    }

    return ApiResult<HttpResponse>::success(std::move(response));
#endif
}

} // namespace api
