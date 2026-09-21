#include "rally_web_server.h"
#include "web_commands.h"
#include "web_telemetry.h"
#include "rally_types.h"

#include <gio/gio.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>

struct WsClient {
    int fd = -1;
    std::string rx_buf;
};

struct WebServerState {
    AppData* data = nullptr;
    int listen_fd = -1;
    gulong listen_source = 0;
    std::vector<WsClient> clients;
    int64_t last_telemetry_ms = 0;
    int port = 8080;
    std::string lan_ip = "127.0.0.1";
};

namespace {

std::string g_webclient_dir;
bool g_state_dirty = false;

std::string getExecutableDir() {
    char path[4096];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len <= 0) return ".";
    path[len] = '\0';
    std::string s(path);
    size_t slash = s.find_last_of('/');
    if (slash == std::string::npos) return ".";
    return s.substr(0, slash);
}

std::string resolveWebclientDir() {
    std::string base = getExecutableDir();
    std::string candidate = base + "/webclient";
    if (access((candidate + "/index.html").c_str(), R_OK) == 0) return candidate;
    candidate = base + "/../webclient";
    if (access((candidate + "/index.html").c_str(), R_OK) == 0) return candidate;
    return base + "/webclient";
}

std::string base64Encode(const unsigned char* data, size_t len) {
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        unsigned val = data[i] << 16;
        if (i + 1 < len) val |= data[i + 1] << 8;
        if (i + 2 < len) val |= data[i + 2];
        out.push_back(tbl[(val >> 18) & 63]);
        out.push_back(tbl[(val >> 12) & 63]);
        out.push_back(i + 1 < len ? tbl[(val >> 6) & 63] : '=');
        out.push_back(i + 2 < len ? tbl[val & 63] : '=');
    }
    return out;
}

std::string sha1Base64(const std::string& input) {
    guchar digest[20];
    gsize digest_len = 20;
    GChecksum* checksum = g_checksum_new(G_CHECKSUM_SHA1);
    g_checksum_update(checksum, reinterpret_cast<const guchar*>(input.data()), input.size());
    g_checksum_get_digest(checksum, digest, &digest_len);
    g_checksum_free(checksum);
    return base64Encode(digest, digest_len);
}

std::string getHeaderValue(const std::string& request, const char* header) {
    std::string key = std::string(header) + ":";
    size_t pos = request.find(key);
    if (pos == std::string::npos) return {};
    pos += key.size();
    while (pos < request.size() && request[pos] == ' ') pos++;
    size_t end = request.find("\r\n", pos);
    if (end == std::string::npos) end = request.size();
    return request.substr(pos, end - pos);
}

std::string getRequestPath(const std::string& request) {
    size_t start = request.find(' ');
    if (start == std::string::npos) return "/";
    start++;
    size_t end = request.find(' ', start);
    if (end == std::string::npos) return "/";
    return request.substr(start, end - start);
}

std::string readStaticFile(const std::string& rel) {
    std::string path = g_webclient_dir + rel;
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

std::string wsEncodeText(const std::string& payload) {
    std::string frame;
    frame.push_back(static_cast<char>(0x81));
    size_t len = payload.size();
    if (len <= 125) {
        frame.push_back(static_cast<char>(len));
    } else if (len <= 65535) {
        frame.push_back(126);
        frame.push_back(static_cast<char>((len >> 8) & 0xFF));
        frame.push_back(static_cast<char>(len & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i)
            frame.push_back(static_cast<char>((len >> (8 * i)) & 0xFF));
    }
    frame += payload;
    return frame;
}

bool wsDecodeText(const std::string& data, std::string& out, size_t& consumed) {
    consumed = 0;
    if (data.size() < 2) return false;
    unsigned char b0 = static_cast<unsigned char>(data[0]);
    unsigned char b1 = static_cast<unsigned char>(data[1]);
    if ((b0 & 0x0F) != 0x01) return false;
    bool masked = b1 & 0x80;
    size_t len = b1 & 0x7F;
    size_t pos = 2;
    if (len == 126) {
        if (data.size() < 4) return false;
        len = (static_cast<unsigned char>(data[2]) << 8) |
              static_cast<unsigned char>(data[3]);
        pos = 4;
    } else if (len == 127) {
        if (data.size() < 10) return false;
        len = 0;
        for (int i = 0; i < 8; i++)
            len = (len << 8) | static_cast<unsigned char>(data[2 + i]);
        pos = 10;
    }
    unsigned char mask[4] = {0, 0, 0, 0};
    if (masked) {
        if (data.size() < pos + 4) return false;
        memcpy(mask, data.data() + pos, 4);
        pos += 4;
    }
    if (data.size() < pos + len) return false;
    out.resize(len);
    for (size_t i = 0; i < len; i++) {
        unsigned char c = static_cast<unsigned char>(data[pos + i]);
        if (masked) c ^= mask[i % 4];
        out[i] = static_cast<char>(c);
    }
    consumed = pos + len;
    return true;
}

static WebServerState* g_impl = nullptr;

static void closeClient(WsClient& c) {
    if (c.fd >= 0) {
        close(c.fd);
        c.fd = -1;
    }
}

static void broadcastAll(WebServerState* impl, const std::string& msg) {
    std::string frame = wsEncodeText(msg);
    for (auto& c : impl->clients) {
        if (c.fd < 0) continue;
        ssize_t n = send(c.fd, frame.data(), frame.size(), MSG_NOSIGNAL);
        if (n < 0) closeClient(c);
    }
    impl->clients.erase(
        std::remove_if(impl->clients.begin(), impl->clients.end(),
                       [](const WsClient& c) { return c.fd < 0; }),
        impl->clients.end());
}

static void sendHttpResponse(int fd, int code, const char* status,
                             const std::string& content_type, const std::string& body) {
    char header[512];
    snprintf(header, sizeof(header),
             "HTTP/1.1 %d %s\r\n"
             "Connection: close\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "Cache-Control: no-cache\r\n"
             "\r\n",
             code, status, content_type.c_str(), body.size());
    send(fd, header, strlen(header), MSG_NOSIGNAL);
    if (!body.empty()) send(fd, body.data(), body.size(), MSG_NOSIGNAL);
}

static bool handleHttpRequest(WebServerState* impl, int fd, const std::string& request) {
    std::string path = getRequestPath(request);
    if (path == "/ws") {
        std::string key = getHeaderValue(request, "Sec-WebSocket-Key");
        if (key.empty()) {
            sendHttpResponse(fd, 400, "Bad Request", "text/plain", "Missing Sec-WebSocket-Key");
            return false;
        }
        std::string accept = sha1Base64(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
        char header[512];
        snprintf(header, sizeof(header),
                 "HTTP/1.1 101 Switching Protocols\r\n"
                 "Upgrade: websocket\r\n"
                 "Connection: Upgrade\r\n"
                 "Sec-WebSocket-Accept: %s\r\n"
                 "\r\n",
                 accept.c_str());
        send(fd, header, strlen(header), MSG_NOSIGNAL);
        setNonBlocking(fd);
        impl->clients.push_back({fd, {}});
        broadcastAll(impl, buildStateJson(impl->data));
        return true;
    }

    if (path == "/" || path == "/index.html") {
        std::string body = readStaticFile("/index.html");
        if (body.empty()) body = "<html><body>webclient missing</body></html>";
        sendHttpResponse(fd, 200, "OK", "text/html; charset=utf-8", body);
        return false;
    }
    if (path == "/app.css") {
        sendHttpResponse(fd, 200, "OK", "text/css; charset=utf-8", readStaticFile("/app.css"));
        return false;
    }
    if (path == "/app.js") {
        sendHttpResponse(fd, 200, "OK", "application/javascript; charset=utf-8", readStaticFile("/app.js"));
        return false;
    }
    if (path == "/gauge.js") {
        sendHttpResponse(fd, 200, "OK", "application/javascript; charset=utf-8", readStaticFile("/gauge.js"));
        return false;
    }
    sendHttpResponse(fd, 404, "Not Found", "text/plain", "Not found");
    return false;
}

static void serviceWsClients(WebServerState* impl) {
    for (auto& client : impl->clients) {
        if (client.fd < 0) continue;
        char buf[4096];
        while (true) {
            ssize_t n = recv(client.fd, buf, sizeof(buf), 0);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                closeClient(client);
                break;
            }
            if (n == 0) {
                closeClient(client);
                break;
            }
            client.rx_buf.append(buf, static_cast<size_t>(n));
        }

        while (true) {
            std::string msg;
            size_t consumed = 0;
            if (!wsDecodeText(client.rx_buf, msg, consumed)) break;
            client.rx_buf.erase(0, consumed);
            if (webHandleCommand(impl->data, msg.c_str())) {
                g_state_dirty = true;
                broadcastAll(impl, buildStateJson(impl->data));
            }
        }
    }
    impl->clients.erase(
        std::remove_if(impl->clients.begin(), impl->clients.end(),
                       [](const WsClient& c) { return c.fd < 0; }),
        impl->clients.end());
}

static gboolean onListen(GIOChannel*, GIOCondition, gpointer user_data) {
    auto* impl = static_cast<WebServerState*>(user_data);
    sockaddr_in addr{};
    socklen_t addrlen = sizeof(addr);
    int fd = accept(impl->listen_fd, reinterpret_cast<sockaddr*>(&addr), &addrlen);
    if (fd < 0) return G_SOURCE_CONTINUE;

    char buf[8192];
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        close(fd);
        return G_SOURCE_CONTINUE;
    }
    buf[n] = '\0';
    std::string request(buf);

    bool keep_open = handleHttpRequest(impl, fd, request);
    if (!keep_open) close(fd);
    return G_SOURCE_CONTINUE;
}

static std::string detectLanIp() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return "127.0.0.1";
    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(80);
    inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);
    if (connect(fd, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) < 0) {
        close(fd);
        return "127.0.0.1";
    }
    sockaddr_in local{};
    socklen_t len = sizeof(local);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&local), &len) < 0) {
        close(fd);
        return "127.0.0.1";
    }
    close(fd);
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &local.sin_addr, ip, sizeof(ip));
    return ip;
}

} // namespace

RallyWebServer::RallyWebServer(AppData* data)
    : impl_(new WebServerState), data_(data) {
    auto* s = static_cast<WebServerState*>(impl_);
    s->data = data;
    g_webclient_dir = resolveWebclientDir();
    g_impl = s;
}

RallyWebServer::~RallyWebServer() {
    stop();
    delete static_cast<WebServerState*>(impl_);
    if (g_impl == static_cast<WebServerState*>(impl_)) g_impl = nullptr;
}

bool RallyWebServer::start(int port) {
    auto* impl = static_cast<WebServerState*>(impl_);
    stop();
    impl->port = port;
    impl->lan_ip = detectLanIp();

    impl->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (impl->listen_fd < 0) return false;

    int yes = 1;
    setsockopt(impl->listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    setNonBlocking(impl->listen_fd);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (bind(impl->listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        stop();
        return false;
    }
    if (listen(impl->listen_fd, 16) < 0) {
        stop();
        return false;
    }

    GIOChannel* ch = g_io_channel_unix_new(impl->listen_fd);
    g_io_channel_set_close_on_unref(ch, FALSE);
    impl->listen_source = g_io_add_watch(ch, G_IO_IN, onListen, impl);
    g_io_channel_unref(ch);

    impl->last_telemetry_ms = 0;
    return true;
}

void RallyWebServer::stop() {
    auto* impl = static_cast<WebServerState*>(impl_);
    for (auto& c : impl->clients) closeClient(c);
    impl->clients.clear();
    if (impl->listen_source) {
        g_source_remove(impl->listen_source);
        impl->listen_source = 0;
    }
    if (impl->listen_fd >= 0) {
        close(impl->listen_fd);
        impl->listen_fd = -1;
    }
}

void RallyWebServer::poll(int64_t now_ms) {
    auto* impl = static_cast<WebServerState*>(impl_);
    if (impl->listen_fd < 0) return;
    serviceWsClients(impl);

    if (now_ms - impl->last_telemetry_ms >= 100) {
        impl->last_telemetry_ms = now_ms;
        broadcastAll(impl, buildTelemetryJson(data_));
    }
    if (g_state_dirty) {
        g_state_dirty = false;
        broadcastAll(impl, buildStateJson(data_));
    }
}

std::string RallyWebServer::getWebUrl() const {
    auto* impl = static_cast<const WebServerState*>(impl_);
    char buf[256];
    snprintf(buf, sizeof(buf), "http://%s:%d/", impl->lan_ip.c_str(), impl->port);
    return buf;
}

void webNotifyStateChanged(AppData* data) {
    (void)data;
    g_state_dirty = true;
}
