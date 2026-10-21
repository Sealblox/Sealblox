#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace http
{
    struct Response
    {
        long status = 0;
        std::vector<uint8_t> body;
        std::string error;
        bool ok() const { return error.empty() && status >= 200 && status < 300; }

        std::string text() const { return std::string(body.begin(), body.end()); }
    };

    using Headers = std::vector<std::pair<std::string, std::string>>;

    struct RequestOptions
    {
        long timeoutSeconds = 60;
        long connectTimeoutSeconds = 15;
        size_t maxBytes = 512u * 1024u * 1024u;
        const std::atomic<bool> *cancel = nullptr;
        Headers headers;
    };

    void globalInit();
    void globalShutdown();

    Response get(const std::string &url, const RequestOptions &options = {});

    Response post(const std::string &url, const std::string &body, const RequestOptions &options = {});

    bool isUrl(const std::string &source);
}