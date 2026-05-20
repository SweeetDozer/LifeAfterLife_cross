#pragma once

#include "api_client.h"

#include <string>

namespace api {

struct LoginResponse {
    std::string access_token;
    std::string refresh_token;
    std::string token_type;
};

struct RegisterResponse {
    std::string detail;
};

struct LogoutResponse {
    std::string detail;
};

class AuthApi {
public:
    explicit AuthApi(ApiClient& api_client);

    ApiResult<LoginResponse> login(const std::string& email, const std::string& password);
    ApiResult<RegisterResponse> register_user(const std::string& email, const std::string& password);
    ApiResult<LogoutResponse> logout(const std::string& refresh_token);

private:
    ApiClient& api_client_;
};

} // namespace api
