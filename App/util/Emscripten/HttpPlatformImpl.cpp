#include "util/HttpPlatformImpl.h"
#include "util/Http.h"
#include <string>
#include <emscripten/fetch.h>
#include <emscripten/websocket.h>
#include <boost/filesystem.hpp>

LOGGROUP(Http)
DYNAMIC_LOGGROUP(HttpTrace)

using namespace RBX;
using namespace RBX::HttpPlatformImpl;

namespace RBX
{
namespace HttpPlatformImpl
{
    EMSCRIPTEN_WEBSOCKET_T ws_conn;
    void setProxy(const std::string& host, long port)
    {}

    void init(Http::CookieSharingPolicy cookieSharingPolicy)
    {}

    void setCookiesForDomain(const std::string& domain, const std::string& cookies)
    {}

    void setCookiesForDomain(const std::string& domain, std::string& cookies)
    {}

    boost::filesystem::path getRobloxCookieJarPath()
    {
        return "";
    }

    void perform(HttpOptions& options, std::string& response)
    {
        // Rewrite any roblox.com asset URL to the assetdelivery CDN.
        // ContentId::reconstructUrl() prepends /api to asset paths, producing
        //   https://www.roblox.com/api/asset?id=###
        // Older code / assetgame subdomain produces:
        //   http(s)://www.roblox.com/asset/?id=###
        //   http(s)://assetgame.roblox.com/asset/?id=###
        // Both are dead endpoints; assetdelivery.roblox.com/v1/asset?id=### is live.
        std::string fetchUrl = options.url;
        {
            const std::string kIdParam = "?id=";
            auto idPos = fetchUrl.find(kIdParam);
            bool isRobloxDomain = fetchUrl.find("roblox.com") != std::string::npos;
            if (isRobloxDomain && idPos != std::string::npos)
            {
                // Rewrite if the path portion contains "/asset"
                if (fetchUrl.substr(0, idPos).find("/asset") != std::string::npos)
                {
                    std::string assetId = fetchUrl.substr(idPos + kIdParam.size());
                    // Strip any extra query params after the id value
                    auto ampPos = assetId.find('&');
                    if (ampPos != std::string::npos)
                        assetId = assetId.substr(0, ampPos);
                    fetchUrl = "https://assetdelivery.roblox.com/v1/asset?id=" + assetId;
                }
            }
        }

        // Route roblox.com and rbxcdn.com requests through the CORS proxy.
        // Uses a RELATIVE path (no leading slash) so the URL resolves relative to
        // the page's directory.  This keeps the request inside the service worker's
        // scope even when the page is served from a subdirectory (e.g. GitHub Pages
        // at /repo/web/).  coi-sw.js intercepts /proxy/* and forwards server-side.
        // serve.py also handles /proxy/* on localhost (sees the path as /proxy/...).
        if (fetchUrl.find("roblox.com") != std::string::npos ||
            fetchUrl.find("rbxcdn.com") != std::string::npos)
        {
            std::string noScheme = fetchUrl;
            if (noScheme.compare(0, 8, "https://") == 0)      noScheme = noScheme.substr(8);
            else if (noScheme.compare(0, 7, "http://") == 0)   noScheme = noScheme.substr(7);
            fetchUrl = "proxy/" + noScheme;
        }

        emscripten_fetch_attr_t attr;
        emscripten_fetch_attr_init(&attr);
        attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_SYNCHRONOUS;

        if (options.postData)
        {
            strcpy(attr.requestMethod, "POST");
        }
        else
        {
            strcpy(attr.requestMethod, "GET");
        }

        emscripten_fetch_t *fetch = emscripten_fetch(&attr, fetchUrl.c_str());
        long statusCode = fetch->status;

        if (statusCode < 200 || statusCode > 308 || statusCode == 202)
        {
            std::string errMsg = "HTTP " + std::to_string(statusCode) + " fetching " + fetchUrl;
            emscripten_fetch_close(fetch);
            throw RBX::http_status_error(statusCode, errMsg);
        }
        else
        {
            response.assign(fetch->data, fetch->numBytes);
        }

        emscripten_fetch_close(fetch);
    }
} // namespace HttpPlatformImpl
} // namespace RBX