// ============================================================
//  Personal Website Server  —— 纯 C++17 实现
//  - 静态资源服务 (www/ 目录)
//  - 个人作品链接管理 REST API (JSON 文件持久化)
//  依赖: cpp-httplib (单头文件)  链接: ws2_32 winmm
// ============================================================
#include "httplib.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

// -------------------- 极简 JSON 实现 --------------------
namespace minijson {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string str;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;

    const Value* find(const std::string& key) const {
        if (type != Type::Object) return nullptr;
        for (const auto& kv : obj) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }

    std::string asString(const std::string& def = "") const {
        return type == Type::String ? str : def;
    }
};

class ParseError : public std::runtime_error {
public:
    explicit ParseError(const std::string& msg) : std::runtime_error(msg) {}
};

class Parser {
public:
    explicit Parser(const std::string& text) : s_(text), i_(0) {}

    Value parse() {
        skipWs();
        Value v = parseValue();
        skipWs();
        if (i_ != s_.size()) throw ParseError("trailing characters");
        return v;
    }

private:
    const std::string& s_;
    size_t i_;

    void skipWs() {
        while (i_ < s_.size()) {
            char c = s_[i_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++i_;
            } else {
                break;
            }
        }
    }

    char peek() {
        if (i_ >= s_.size()) throw ParseError("unexpected end");
        return s_[i_];
    }

    char get() {
        if (i_ >= s_.size()) throw ParseError("unexpected end");
        return s_[i_++];
    }

    void expect(char c) {
        if (get() != c) throw ParseError(std::string("expected '") + c + "'");
    }

    Value parseValue() {
        skipWs();
        char c = peek();
        switch (c) {
            case '{': return parseObject();
            case '[': return parseArray();
            case '"': {
                Value v;
                v.type = Value::Type::String;
                v.str = parseString();
                return v;
            }
            case 't':
            case 'f': return parseBool();
            case 'n': return parseNull();
            default:  return parseNumber();
        }
    }

    Value parseObject() {
        Value v;
        v.type = Value::Type::Object;
        expect('{');
        skipWs();
        if (peek() == '}') { get(); return v; }
        while (true) {
            skipWs();
            std::string key = parseString();
            skipWs();
            expect(':');
            v.obj.emplace_back(std::move(key), parseValue());
            skipWs();
            char c = get();
            if (c == '}') break;
            if (c != ',') throw ParseError("expected ',' or '}'");
        }
        return v;
    }

    Value parseArray() {
        Value v;
        v.type = Value::Type::Array;
        expect('[');
        skipWs();
        if (peek() == ']') { get(); return v; }
        while (true) {
            v.arr.push_back(parseValue());
            skipWs();
            char c = get();
            if (c == ']') break;
            if (c != ',') throw ParseError("expected ',' or ']'");
        }
        return v;
    }

    std::string parseString() {
        expect('"');
        std::string out;
        while (true) {
            char c = get();
            if (c == '"') break;
            if (c == '\\') {
                char e = get();
                switch (e) {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': {
                        unsigned code = 0;
                        for (int k = 0; k < 4; ++k) {
                            char h = get();
                            code <<= 4;
                            if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
                            else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                            else throw ParseError("bad unicode escape");
                        }
                        // UTF-8 编码 (不处理代理对合并, BMP 内足够使用)
                        if (code < 0x80) {
                            out.push_back(static_cast<char>(code));
                        } else if (code < 0x800) {
                            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        } else {
                            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        }
                        break;
                    }
                    default: throw ParseError("bad escape");
                }
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

    Value parseBool() {
        if (s_.compare(i_, 4, "true") == 0) { i_ += 4; Value v; v.type = Value::Type::Bool; v.boolean = true; return v; }
        if (s_.compare(i_, 5, "false") == 0) { i_ += 5; Value v; v.type = Value::Type::Bool; return v; }
        throw ParseError("bad literal");
    }

    Value parseNull() {
        if (s_.compare(i_, 4, "null") == 0) { i_ += 4; return Value(); }
        throw ParseError("bad literal");
    }

    Value parseNumber() {
        size_t start = i_;
        if (peek() == '-') ++i_;
        while (i_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[i_])) ||
                                  s_[i_] == '.' || s_[i_] == 'e' || s_[i_] == 'E' ||
                                  s_[i_] == '+' || s_[i_] == '-')) {
            ++i_;
        }
        if (start == i_) throw ParseError("bad number");
        Value v;
        v.type = Value::Type::Number;
        v.number = std::stod(s_.substr(start, i_ - start));
        return v;
    }
};

inline std::string escape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (unsigned char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

}  // namespace minijson

// -------------------- 业务模型 --------------------
struct Project {
    std::uint64_t id = 0;
    std::string title;
    std::string description;
    std::string url;
    std::vector<std::string> tags;
    std::int64_t createdAt = 0;  // unix 秒
};

class ProjectStore {
public:
    explicit ProjectStore(std::string file) : file_(std::move(file)) {
        load();
    }

    std::vector<Project> all() {
        std::lock_guard<std::mutex> lock(mtx_);
        return projects_;
    }

    Project add(const std::string& title, const std::string& desc,
                const std::string& url, const std::vector<std::string>& tags) {
        std::lock_guard<std::mutex> lock(mtx_);
        Project p;
        p.id = nextId();
        p.title = title;
        p.description = desc;
        p.url = url;
        p.tags = tags;
        p.createdAt = std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
        projects_.push_back(p);
        saveLocked();
        return p;
    }

    bool remove(std::uint64_t id) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = std::find_if(projects_.begin(), projects_.end(),
                               [id](const Project& p) { return p.id == id; });
        if (it == projects_.end()) return false;
        projects_.erase(it);
        saveLocked();
        return true;
    }

private:
    std::string file_;
    std::vector<Project> projects_;
    std::mutex mtx_;

    std::uint64_t nextId() const {
        std::uint64_t maxId = 0;
        for (const auto& p : projects_) maxId = std::max(maxId, p.id);
        return maxId + 1;
    }

    void load() {
        std::ifstream in(file_, std::ios::binary);
        if (!in) {
            seed();
            return;
        }
        std::stringstream ss;
        ss << in.rdbuf();
        try {
            minijson::Value root = minijson::Parser(ss.str()).parse();
            if (root.type != minijson::Value::Type::Array) return;
            for (const auto& item : root.arr) {
                Project p;
                if (const auto* v = item.find("id")) p.id = static_cast<std::uint64_t>(v->number);
                if (const auto* v = item.find("title")) p.title = v->asString();
                if (const auto* v = item.find("description")) p.description = v->asString();
                if (const auto* v = item.find("url")) p.url = v->asString();
                if (const auto* v = item.find("createdAt")) p.createdAt = static_cast<std::int64_t>(v->number);
                if (const auto* tags = item.find("tags"); tags && tags->type == minijson::Value::Type::Array) {
                    for (const auto& t : tags->arr) {
                        if (t.type == minijson::Value::Type::String) p.tags.push_back(t.str);
                    }
                }
                projects_.push_back(std::move(p));
            }
        } catch (const std::exception&) {
            // 文件损坏时从空开始, 避免服务起不来
            projects_.clear();
        }
    }

    void seed() {
        Project p;
        p.id = 1;
        p.title = "示例作品：我的 GitHub 主页";
        p.description = "这是一条示例数据。点击页面上的「添加作品」按钮，即可把你的项目链接展示在这里。";
        p.url = "https://github.com";
        p.tags = {"示例", "C++"};
        p.createdAt = std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
        projects_.push_back(std::move(p));
        saveLocked();
    }

    void saveLocked() {
        std::ofstream out(file_, std::ios::binary | std::ios::trunc);
        out << "[\n";
        for (size_t i = 0; i < projects_.size(); ++i) {
            const Project& p = projects_[i];
            out << "  {\n";
            out << "    \"id\": " << p.id << ",\n";
            out << "    \"title\": \"" << minijson::escape(p.title) << "\",\n";
            out << "    \"description\": \"" << minijson::escape(p.description) << "\",\n";
            out << "    \"url\": \"" << minijson::escape(p.url) << "\",\n";
            out << "    \"createdAt\": " << p.createdAt << ",\n";
            out << "    \"tags\": [";
            for (size_t j = 0; j < p.tags.size(); ++j) {
                if (j) out << ", ";
                out << "\"" << minijson::escape(p.tags[j]) << "\"";
            }
            out << "]\n";
            out << "  }" << (i + 1 == projects_.size() ? "\n" : ",\n");
        }
        out << "]\n";
    }
};

// -------------------- 工具函数 --------------------
static std::string exeDir() {
#ifdef _WIN32
    char buf[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string p(buf);
    size_t pos = p.find_last_of("\\/");
    return pos == std::string::npos ? "." : p.substr(0, pos);
#else
    char buf[PATH_MAX] = {0};
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return ".";
    std::string p(buf, static_cast<size_t>(n));
    size_t pos = p.find_last_of('/');
    return pos == std::string::npos ? "." : p.substr(0, pos);
#endif
}

static void makeDir(const std::string& path) {
#ifdef _WIN32
    CreateDirectoryA(path.c_str(), nullptr);
#else
    ::mkdir(path.c_str(), 0755);
#endif
}

static std::string projectToJson(const Project& p) {
    std::ostringstream os;
    os << '{';
    os << "\"id\":" << p.id << ',';
    os << "\"title\":\"" << minijson::escape(p.title) << "\",";
    os << "\"description\":\"" << minijson::escape(p.description) << "\",";
    os << "\"url\":\"" << minijson::escape(p.url) << "\",";
    os << "\"createdAt\":" << p.createdAt << ',';
    os << "\"tags\":[";
    for (size_t i = 0; i < p.tags.size(); ++i) {
        if (i) os << ',';
        os << '"' << minijson::escape(p.tags[i]) << '"';
    }
    os << "]}";
    return os.str();
}

static std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static bool isValidUrl(const std::string& url) {
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) return false;
    return url.size() >= 12;  // http://x.cn 级别
}

// -------------------- 主程序 --------------------
int main() {
    const std::string base = exeDir();
    const std::string wwwDir = base + "/www";
    const std::string dataDir = base + "/data";
    makeDir(dataDir);

    auto store = std::make_shared<ProjectStore>(dataDir + "/projects.json");

    httplib::Server svr;

    svr.set_default_headers({{"Server", "CppPersonalWeb/1.0"}});

    svr.set_logger([](const httplib::Request& req, const httplib::Response& res) {
        printf("[%s] %s %s -> %d\n",
               std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count())
                   .c_str(),
               req.method.c_str(), req.path.c_str(), res.status);
    });

    // 健康检查
    svr.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    // 获取全部作品
    svr.Get("/api/projects", [store](const httplib::Request&, httplib::Response& res) {
        std::vector<Project> list = store->all();
        std::ostringstream os;
        os << '[';
        for (size_t i = 0; i < list.size(); ++i) {
            if (i) os << ',';
            os << projectToJson(list[i]);
        }
        os << ']';
        res.set_content(os.str(), "application/json");
    });

    // 添加作品
    svr.Post("/api/projects", [store](const httplib::Request& req, httplib::Response& res) {
        minijson::Value body;
        try {
            body = minijson::Parser(req.body).parse();
        } catch (const std::exception&) {
            res.status = 400;
            res.set_content(R"({"error":"请求体不是合法 JSON"})", "application/json");
            return;
        }
        if (body.type != minijson::Value::Type::Object) {
            res.status = 400;
            res.set_content(R"({"error":"请求体必须是 JSON 对象"})", "application/json");
            return;
        }

        auto getStr = [&](const char* key) -> std::string {
            const minijson::Value* v = body.find(key);
            return v ? v->asString() : std::string();
        };

        std::string title = trim(getStr("title"));
        std::string desc = trim(getStr("description"));
        std::string url = trim(getStr("url"));

        if (title.empty() || title.size() > 100) {
            res.status = 400;
            res.set_content(R"({"error":"标题必填且不能超过 100 字"})", "application/json");
            return;
        }
        if (!isValidUrl(url)) {
            res.status = 400;
            res.set_content(R"({"error":"链接必须以 http:// 或 https:// 开头"})", "application/json");
            return;
        }
        if (desc.size() > 500) {
            res.status = 400;
            res.set_content(R"({"error":"描述不能超过 500 字"})", "application/json");
            return;
        }

        std::vector<std::string> tags;
        if (const auto* t = body.find("tags"); t && t->type == minijson::Value::Type::Array) {
            for (const auto& v : t->arr) {
                if (v.type != minijson::Value::Type::String) continue;
                std::string tag = trim(v.str);
                if (!tag.empty() && tag.size() <= 20) tags.push_back(tag);
                if (tags.size() >= 8) break;
            }
        }

        Project p = store->add(title, desc, url, tags);
        res.status = 201;
        res.set_content(projectToJson(p), "application/json");
    });

    // 删除作品
    svr.Delete(R"(/api/projects/(\d+))", [store](const httplib::Request& req, httplib::Response& res) {
        std::uint64_t id = std::stoull(req.matches[1]);
        if (!store->remove(id)) {
            res.status = 404;
            res.set_content(R"({"error":"作品不存在"})", "application/json");
            return;
        }
        res.set_content(R"({"ok":true})", "application/json");
    });

    // 静态资源
    if (!svr.set_mount_point("/", wwwDir)) {
        fprintf(stderr, "静态目录挂载失败: %s\n", wwwDir.c_str());
        return 1;
    }

    // 自定义 404 (仅页面路径; /api/ 下保留 JSON 错误体)
    svr.set_error_handler([](const httplib::Request& req, httplib::Response& res) {
        if (res.status == 404 && req.path.rfind("/api/", 0) != 0) {
            std::string body = "<!doctype html><meta charset=utf-8><title>404</title>"
                               "<body style='background:#0a0e1a;color:#7dd3fc;font-family:sans-serif;"
                               "display:grid;place-items:center;height:100vh;margin:0'>"
                               "<h1>404 - 页面走丢了 <a style='color:#a78bfa' href='/'>返回首页</a></h1>";
            res.set_content(body, "text/html; charset=utf-8");
        }
    });

    int port = 8080;
    if (const char* envPort = std::getenv("PORT")) {
        int v = std::atoi(envPort);
        if (v > 0 && v < 65536) port = v;
    }

    printf("====================================================\n");
    printf("   C++ 个人网站服务器已启动\n");
    printf("   本机访问 : http://localhost:%d\n", port);
    printf("   局域网   : http://<本机IP>:%d\n", port);
    printf("   静态目录 : %s\n", wwwDir.c_str());
    printf("   数据文件 : %s/projects.json\n", dataDir.c_str());
    printf("   按 Ctrl+C 停止服务\n");
    printf("====================================================\n");

    if (!svr.listen("0.0.0.0", port)) {
        fprintf(stderr, "服务器启动失败, 端口 %d 可能被占用\n", port);
        return 1;
    }
    return 0;
}
