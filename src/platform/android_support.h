#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <filesystem>
#include <optional>
#include <vector>

namespace platform::android {

struct HttpHeader {
    std::string name;
    std::string value;
};

struct HttpRequest {
    std::string method;
    std::string url;
    std::vector<HttpHeader> headers;
    std::string body;
    int connect_timeout_ms = 15000;
    int read_timeout_ms = 15000;
};

struct HttpResponse {
    int status_code = 0;
    std::string body;
};

std::optional<HttpResponse> http_request(const HttpRequest& request, std::string& error_message);
std::optional<std::filesystem::path> files_directory(std::string& error_message);

} // namespace platform::android
