#ifndef CLIJUDGE_HTTP_CLIENT_H
#define CLIJUDGE_HTTP_CLIENT_H

// http_client.hpp
// 自包含 HTTP/HTTPS GET 客户端 (仅 POSIX: Linux / Android/Termux)
//
// 设计目标: 消除 displaylang 联网下载对外部命令 `curl` 的依赖, 保持仓库
// "零外部依赖"。实现方式:
//   · 仅使用 POSIX socket (getaddrinfo/connect/poll/recv/send)
//   · HTTPS 通过运行时 dlopen 加载系统 OpenSSL (libssl), 不链接 -lssl/crypto,
//     因此编译期与链接期均无新增依赖, Android NDK 亦可直接编译
//   · 与 Windows 侧 WinHTTP 实现行为对齐: 不校验证书、跟随重定向、
//     非 2xx 视为失败返回空串
//
// 支持: http/https、连接与读写超时(30s)、重定向(≤5 跳)、
//       Transfer-Encoding: chunked、Content-Length

#ifndef _WIN32

#include <string>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>

namespace clijudge {
namespace http {

namespace detail {

// ── 运行时加载的 OpenSSL 函数表 ───────────────────────────────
// 只声明所需符号, 避免依赖 openssl 开发头文件 (构建环境可能未安装)
struct SslApi {
    void* h = nullptr;
    bool ok = false;
    void* (*client_method)() = nullptr;
    void* (*ctx_new)(const void*) = nullptr;
    void  (*ctx_free)(void*) = nullptr;
    void  (*ctx_set_verify)(void*, int, void*) = nullptr;
    void* (*ssl_new)(void*) = nullptr;
    void  (*ssl_free)(void*) = nullptr;
    int   (*set_fd)(void*, int) = nullptr;
    long  (*ctrl)(void*, int, long, void*) = nullptr;
    int   (*connect)(void*) = nullptr;
    int   (*read)(void*, void*, int) = nullptr;
    int   (*write)(void*, const void*, int) = nullptr;
    int   (*get_error)(const void*, int) = nullptr;
};

template <typename T>
inline bool sym(void* h, T& fn, const char* name) {
    fn = reinterpret_cast<T>(dlsym(h, name));
    return fn != nullptr;
}

inline SslApi& sslApi() {
    static SslApi api;
    static bool tried = false;
    if (tried) return api;
    tried = true;
    const char* cands[] = {"libssl.so.3", "libssl.so", "libssl.so.1.1", nullptr};
    for (int i = 0; cands[i]; i++) {
        api.h = dlopen(cands[i], RTLD_NOW | RTLD_GLOBAL);
        if (api.h) break;
    }
    if (!api.h) return api;
    bool ok = true;
    ok &= sym(api.h, api.client_method,   "TLS_client_method");
    ok &= sym(api.h, api.ctx_new,         "SSL_CTX_new");
    ok &= sym(api.h, api.ctx_free,        "SSL_CTX_free");
    ok &= sym(api.h, api.ctx_set_verify,  "SSL_CTX_set_verify");
    ok &= sym(api.h, api.ssl_new,         "SSL_new");
    ok &= sym(api.h, api.ssl_free,        "SSL_free");
    ok &= sym(api.h, api.set_fd,          "SSL_set_fd");
    ok &= sym(api.h, api.ctrl,            "SSL_ctrl");
    ok &= sym(api.h, api.connect,         "SSL_connect");
    ok &= sym(api.h, api.read,            "SSL_read");
    ok &= sym(api.h, api.write,           "SSL_write");
    ok &= sym(api.h, api.get_error,       "SSL_get_error");
    api.ok = ok;
    return api;
}

constexpr int    kTimeoutSec     = 30;
constexpr int    kMaxRedirects   = 5;
constexpr int    kSslVerifyNone  = 0;   // SSL_VERIFY_NONE
constexpr int    kSslCtrlSni     = 55;  // SSL_CTRL_SET_TLSEXT_HOSTNAME
constexpr long   kSniHostName    = 0;   // TLSEXT_NAMETYPE_host_name
constexpr int    kSslErrWantRead = 2;   // SSL_ERROR_WANT_READ
constexpr int    kSslErrWantWrite= 3;   // SSL_ERROR_WANT_WRITE

struct Conn {
    int   fd  = -1;
    void* ssl = nullptr;  // 非空表示已建立 TLS
    void* ctx = nullptr;
    bool  ok  = false;
};

inline bool timedOut(const std::chrono::steady_clock::time_point& start) {
    return std::chrono::steady_clock::now() - start >
           std::chrono::seconds(kTimeoutSec);
}

// 建立 TCP 连接 (非阻塞 connect + poll 超时, 成功后恢复阻塞并设读写超时)
inline Conn connectTcp(const std::string& host, int port) {
    Conn c;
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0) return c;

    for (struct addrinfo* ai = res; ai; ai = ai->ai_next) {
        int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        bool connected = false;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
            connected = true;
        } else if (errno == EINPROGRESS) {
            struct pollfd pfd;
            pfd.fd = fd; pfd.events = POLLOUT; pfd.revents = 0;
            if (poll(&pfd, 1, kTimeoutSec * 1000) > 0) {
                int soerr = 0; socklen_t len = sizeof(soerr);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &len) == 0 && soerr == 0)
                    connected = true;
            }
        }
        if (!connected) { close(fd); continue; }
        fcntl(fd, F_SETFL, flags);
        struct timeval tv;
        tv.tv_sec = kTimeoutSec; tv.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        c.fd = fd;
        c.ok = true;
        break;
    }
    freeaddrinfo(res);
    return c;
}

inline void closeConn(Conn& c) {
    SslApi& a = sslApi();
    if (c.ssl && a.ok) a.ssl_free(c.ssl);
    if (c.ctx && a.ok) a.ctx_free(c.ctx);
    if (c.fd >= 0) close(c.fd);
    c.ssl = nullptr; c.ctx = nullptr; c.fd = -1; c.ok = false;
}

// TLS 握手 (证书校验关闭, 与 Windows 侧一致)
inline bool startTls(Conn& c, const std::string& host) {
    SslApi& a = sslApi();
    if (!a.ok) return false;
    void* ctx = a.ctx_new(a.client_method());
    if (!ctx) return false;
    a.ctx_set_verify(ctx, kSslVerifyNone, nullptr);
    void* ssl = a.ssl_new(ctx);
    if (!ssl) { a.ctx_free(ctx); return false; }
    a.set_fd(ssl, c.fd);
    a.ctrl(ssl, kSslCtrlSni, kSniHostName, (void*)host.c_str());  // SNI
    if (a.connect(ssl) != 1) {
        a.ssl_free(ssl);
        a.ctx_free(ctx);
        return false;
    }
    c.ctx = ctx;
    c.ssl = ssl;
    return true;
}

inline bool writeAll(Conn& c, const std::string& data, bool tls) {
    auto start = std::chrono::steady_clock::now();
    size_t off = 0;
    while (off < data.size()) {
        if (timedOut(start)) return false;
        ssize_t n;
        if (tls) {
            SslApi& a = sslApi();
            n = a.write(c.ssl, data.data() + off, (int)(data.size() - off));
            if (n <= 0) {
                int e = a.get_error(c.ssl, (int)n);
                if (e == kSslErrWantRead || e == kSslErrWantWrite) continue;
                return false;
            }
        } else {
            n = send(c.fd, data.data() + off, data.size() - off, MSG_NOSIGNAL);
            if (n <= 0) {
                if (errno == EINTR) continue;
                return false;
            }
        }
        off += (size_t)n;
    }
    return true;
}

// 一直读到对端关闭 (Connection: close)
inline std::string readAll(Conn& c, bool tls) {
    auto start = std::chrono::steady_clock::now();
    std::string out;
    char buf[8192];
    for (;;) {
        if (timedOut(start)) break;
        ssize_t n;
        if (tls) {
            SslApi& a = sslApi();
            n = a.read(c.ssl, buf, sizeof(buf));
            if (n > 0) { out.append(buf, (size_t)n); continue; }
            int e = a.get_error(c.ssl, (int)n);
            if (e == kSslErrWantRead || e == kSslErrWantWrite) continue;
            break;  // ZERO_RETURN / SYSCALL / 超时
        } else {
            n = recv(c.fd, buf, sizeof(buf), 0);
            if (n > 0) { out.append(buf, (size_t)n); continue; }
            if (n < 0 && errno == EINTR) continue;
            break;  // 0=EOF, <0=错误/超时
        }
    }
    return out;
}

struct ParsedUrl {
    std::string scheme, host, path;
    int port = 0;
    bool ok = false;
};

inline ParsedUrl parseUrl(const std::string& url) {
    ParsedUrl u;
    std::string s = url;
    if (s.compare(0, 7, "http://") == 0) {
        u.scheme = "http"; u.port = 80; s = s.substr(7);
    } else if (s.compare(0, 8, "https://") == 0) {
        u.scheme = "https"; u.port = 443; s = s.substr(8);
    } else {
        return u;
    }
    size_t slash = s.find('/');
    std::string hostport = (slash == std::string::npos) ? s : s.substr(0, slash);
    u.path = (slash == std::string::npos) ? "/" : s.substr(slash);
    size_t colon = hostport.find(':');
    if (colon != std::string::npos) {
        u.host = hostport.substr(0, colon);
        std::string pr = hostport.substr(colon + 1);
        if (pr.empty()) return u;
        long p = 0;
        for (char ch : pr) {
            if (!std::isdigit((unsigned char)ch)) return u;
            p = p * 10 + (ch - '0');
            if (p > 65535) return u;
        }
        u.port = (int)p;
    } else {
        u.host = hostport;
    }
    u.ok = !u.host.empty() && u.port > 0;
    return u;
}

// 解码 Transfer-Encoding: chunked 正文
inline std::string dechunk(const std::string& body) {
    std::string out;
    size_t pos = 0;
    while (pos < body.size()) {
        size_t eol = body.find("\r\n", pos);
        if (eol == std::string::npos) break;
        std::string line = body.substr(pos, eol - pos);
        size_t semi = line.find(';');
        if (semi != std::string::npos) line = line.substr(0, semi);
        if (line.empty()) break;
        unsigned long long sz = 0;
        for (char ch : line) {
            int v;
            if (ch >= '0' && ch <= '9') v = ch - '0';
            else if (ch >= 'a' && ch <= 'f') v = ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F') v = ch - 'A' + 10;
            else return out;  // 非法块长度
            sz = sz * 16 + (unsigned)v;
        }
        pos = eol + 2;
        if (sz == 0) break;
        if (pos + sz > body.size()) { out.append(body, pos, body.size() - pos); break; }
        out.append(body, pos, (size_t)sz);
        pos += (size_t)sz;
        if (body.compare(pos, 2, "\r\n") == 0) pos += 2;
    }
    return out;
}

// 取首行状态码与指定响应头 (大小写不敏感)
inline int parseResponse(const std::string& head, std::string& location,
                         bool& chunked, long long& contentLen) {
    location.clear();
    chunked = false;
    contentLen = -1;
    size_t lineEnd = head.find("\r\n");
    std::string statusLine = (lineEnd == std::string::npos) ? head : head.substr(0, lineEnd);
    size_t sp = statusLine.find(' ');
    int status = (sp == std::string::npos) ? 0 : std::atoi(statusLine.c_str() + sp + 1);

    size_t pos = (lineEnd == std::string::npos) ? head.size() : lineEnd + 2;
    while (pos < head.size()) {
        size_t e = head.find("\r\n", pos);
        if (e == std::string::npos) e = head.size();
        std::string line = head.substr(pos, e - pos);
        pos = e + 2;
        size_t c = line.find(':');
        if (c == std::string::npos) continue;
        std::string key = line.substr(0, c);
        for (char& ch : key) ch = (char)std::tolower((unsigned char)ch);
        std::string val = line.substr(c + 1);
        size_t b = val.find_first_not_of(" \t");
        if (b != std::string::npos) val = val.substr(b);
        if (key == "location") location = val;
        else if (key == "transfer-encoding") {
            std::string lv = val;
            for (char& ch : lv) ch = (char)std::tolower((unsigned char)ch);
            if (lv.find("chunked") != std::string::npos) chunked = true;
        } else if (key == "content-length") {
            contentLen = std::atoll(val.c_str());
        }
    }
    return status;
}

// 单次请求; 重定向经 redirectOut 返回
inline std::string request(const std::string& url, std::string& redirectOut, bool& isRedirect) {
    isRedirect = false;
    redirectOut.clear();
    ParsedUrl u = parseUrl(url);
    if (!u.ok) return "";

    Conn c = connectTcp(u.host, u.port);
    if (!c.ok) return "";

    bool tls = (u.scheme == "https");
    if (tls && !startTls(c, u.host)) { closeConn(c); return ""; }

    std::string resp;
    std::string req = "GET " + u.path + " HTTP/1.1\r\n";
    req += "Host: " + u.host + "\r\n";
    req += "User-Agent: clijudge\r\n";
    req += "Accept: */*\r\n";
    req += "Connection: close\r\n\r\n";
    if (writeAll(c, req, tls)) resp = readAll(c, tls);
    closeConn(c);

    if (resp.empty()) return "";
    size_t hdrEnd = resp.find("\r\n\r\n");
    if (hdrEnd == std::string::npos) return "";
    std::string head = resp.substr(0, hdrEnd);
    std::string body = resp.substr(hdrEnd + 4);

    std::string location;
    bool chunked = false;
    long long contentLen = -1;
    int status = parseResponse(head, location, chunked, contentLen);

    if (status >= 300 && status < 400 && !location.empty()) {
        isRedirect = true;
        redirectOut = location;
        return "";
    }
    if (status < 200 || status >= 300) return "";  // 同 curl -f: 非 2xx 失败
    if (chunked) return dechunk(body);
    if (contentLen >= 0 && (long long)body.size() > contentLen)
        body.resize((size_t)contentLen);
    return body;
}

}  // namespace detail

// HTTP/HTTPS GET, 跟随重定向; 失败(含非 2xx)返回空串
inline std::string get(const std::string& url) {
    std::string cur = url;
    for (int i = 0; i <= detail::kMaxRedirects; i++) {
        std::string loc;
        bool redir = false;
        std::string body = detail::request(cur, loc, redir);
        if (!redir) return body;
        if (loc.compare(0, 7, "http://") == 0 || loc.compare(0, 8, "https://") == 0) {
            cur = loc;
        } else {
            detail::ParsedUrl base = detail::parseUrl(cur);
            if (!base.ok) return "";
            if (loc.empty() || loc[0] != '/') loc = "/" + loc;
            cur = base.scheme + "://" + base.host + loc;
        }
    }
    return "";
}

}  // namespace http
}  // namespace clijudge

#endif  // !_WIN32
#endif  // CLIJUDGE_HTTP_CLIENT_H
