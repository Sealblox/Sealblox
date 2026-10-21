#include <core/http.hpp>

#include <curl/curl.h>

namespace
{
    struct Transfer
    {
        std::vector<uint8_t> *body = nullptr;
        size_t maxBytes = 0;
        bool tooLarge = false;
    };

    size_t writeCallback(char *data, size_t size, size_t count, void *user)
    {
        auto *t = static_cast<Transfer *>(user);
        const size_t bytes = size * count;
        if (t->body->size() + bytes > t->maxBytes)
        {
            t->tooLarge = true;
            return 0;
        }
        t->body->insert(t->body->end(), data, data + bytes);
        return bytes;
    }

    int progressCallback(void *user, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
    {
        const auto *cancel = static_cast<const std::atomic<bool> *>(user);
        return (cancel && cancel->load()) ? 1 : 0;
    }

    http::Response perform(const std::string &url, const http::RequestOptions &options, const std::string *postBody)
    {
        http::Response response;

        CURL *curl = curl_easy_init();
        if (!curl)
        {
            response.error = "curl_easy_init failed";
            return response;
        }

        curl_slist *headers = nullptr;
        for (const auto &header : options.headers)
        {
            const std::string line = header.second.empty()
                                         ? header.first + ";"
                                         : header.first + ": " + header.second;
            curl_slist *appended = curl_slist_append(headers, line.c_str());
            if (!appended)
            {
                curl_slist_free_all(headers);
                curl_easy_cleanup(curl);
                response.error = "failed to build request headers";
                return response;
            }
            headers = appended;
        }

        Transfer transfer;
        transfer.body = &response.body;
        transfer.maxBytes = options.maxBytes;

        char errorBuffer[CURL_ERROR_SIZE] = {};

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, options.timeoutSeconds);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, options.connectTimeoutSeconds);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Construx/1.0");
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);

        if (headers)
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        if (postBody)
        {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(postBody->size()));
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postBody->data());
        }

        if (options.cancel)
        {
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
            curl_easy_setopt(curl, CURLOPT_XFERINFODATA, const_cast<std::atomic<bool> *>(options.cancel));
        }

        const CURLcode result = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);

        if (transfer.tooLarge)
            response.error = "response exceeds size limit";
        else if (result == CURLE_ABORTED_BY_CALLBACK)
            response.error = "cancelled";
        else if (result != CURLE_OK)
            response.error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(result);
        else if (response.status < 200 || response.status >= 300)
            response.error = "HTTP status " + std::to_string(response.status);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return response;
    }
}

namespace http
{
    void globalInit()
    {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    void globalShutdown()
    {
        curl_global_cleanup();
    }

    bool isUrl(const std::string &source)
    {
        return source.rfind("http://", 0) == 0 || source.rfind("https://", 0) == 0;
    }

    Response get(const std::string &url, const RequestOptions &options)
    {
        return perform(url, options, nullptr);
    }

    Response post(const std::string &url, const std::string &body, const RequestOptions &options)
    {
        return perform(url, options, &body);
    }
}