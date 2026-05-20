#include "auth_api.h"

namespace api {

namespace {

std::string escape_json(std::string_view value)
{
    std::string escaped;
    for (const char ch : value) {
        if (ch == '\\' || ch == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

ApiError missing_field_error(std::string field)
{
    return ApiError {
        .type = ApiErrorType::Parse,
        .http_status = 200,
        .message = "Missing field in response: " + field,
    };
}

} // namespace

AuthApi::AuthApi(ApiClient& api_client)
    : api_client_(api_client)
{
}

ApiResult<LoginResponse> AuthApi::login(const std::string& email, const std::string& password)
{
    const std::string request_body =
        "{"
        "\"email\":\"" + escape_json(email) + "\","
        "\"password\":\"" + escape_json(password) + "\""
        "}";

    auto response = api_client_.post_json("/auth/login", request_body);
    if (!response.ok) {
        return ApiResult<LoginResponse>::failure(*response.error);
    }

    const JsonObject* object = as_object(response.value.json_body ? &*response.value.json_body : nullptr);
    if (!object) {
        return ApiResult<LoginResponse>::failure(missing_field_error("login response object"));
    }

    auto access_token = json_get_string(object, "access_token");
    auto refresh_token = json_get_string(object, "refresh_token");
    auto token_type = json_get_string(object, "token_type");
    if (!access_token) {
        return ApiResult<LoginResponse>::failure(missing_field_error("access_token"));
    }
    if (!refresh_token) {
        return ApiResult<LoginResponse>::failure(missing_field_error("refresh_token"));
    }
    if (!token_type) {
        return ApiResult<LoginResponse>::failure(missing_field_error("token_type"));
    }

    return ApiResult<LoginResponse>::success(LoginResponse {
        .access_token = *access_token,
        .refresh_token = *refresh_token,
        .token_type = *token_type,
    });
}

ApiResult<RegisterResponse> AuthApi::register_user(const std::string& email, const std::string& password)
{
    const std::string request_body =
        "{"
        "\"email\":\"" + escape_json(email) + "\","
        "\"password\":\"" + escape_json(password) + "\""
        "}";

    auto response = api_client_.post_json("/auth/register", request_body);
    if (!response.ok) {
        return ApiResult<RegisterResponse>::failure(*response.error);
    }

    const JsonObject* object = as_object(response.value.json_body ? &*response.value.json_body : nullptr);
    if (!object) {
        return ApiResult<RegisterResponse>::failure(missing_field_error("register response object"));
    }

    auto detail = json_get_string(object, "detail");
    if (!detail) {
        return ApiResult<RegisterResponse>::failure(missing_field_error("detail"));
    }

    return ApiResult<RegisterResponse>::success(RegisterResponse {
        .detail = *detail,
    });
}

ApiResult<LogoutResponse> AuthApi::logout(const std::string& refresh_token)
{
    const std::string request_body =
        "{"
        "\"refresh_token\":\"" + escape_json(refresh_token) + "\""
        "}";

    auto response = api_client_.post_json("/auth/logout", request_body);
    if (!response.ok) {
        return ApiResult<LogoutResponse>::failure(*response.error);
    }

    const JsonObject* object = as_object(response.value.json_body ? &*response.value.json_body : nullptr);
    if (!object) {
        return ApiResult<LogoutResponse>::failure(missing_field_error("logout response object"));
    }

    auto detail = json_get_string(object, "detail");
    if (!detail) {
        return ApiResult<LogoutResponse>::failure(missing_field_error("detail"));
    }

    return ApiResult<LogoutResponse>::success(LogoutResponse {
        .detail = *detail,
    });
}

} // namespace api
