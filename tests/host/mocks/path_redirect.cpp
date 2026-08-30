#include "path_redirect.hpp"

#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace hostmock {
namespace {

const std::string &make_root()
{
    static const std::string root = [] {
        std::string tmpl = "/tmp/tab5os_host_tests_XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        const char *dir = mkdtemp(buf.data());
        const std::string base = dir != nullptr ? dir : tmpl;
        /* Provisiona o diretorio padrao do sistema, como o mount real */
        mkdir((base + "/tab5_os").c_str(), 0755);
        mkdir((base + "/.tab5_os").c_str(), 0755);
        return base;
    }();
    return root;
}

const std::string &make_apps_root()
{
    static const std::string apps_root = [] {
        std::string tmpl = "/tmp/tab5os_apps_XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        const char *dir = mkdtemp(buf.data());
        return dir != nullptr ? std::string(dir) : tmpl;
    }();
    return apps_root;
}

} // namespace

const std::string &tmp_root()
{
    return make_root();
}

const std::string &apps_root()
{
    return make_apps_root();
}

std::string redirect_path(const char *path)
{
    if (path == nullptr) {
        return "";
    }
    constexpr const char kPrefix[] = "/sdcard/";
    const size_t prefix_len = sizeof(kPrefix) - 1;
    if (std::strncmp(path, kPrefix, prefix_len) == 0) {
        return tmp_root() + "/" + (path + prefix_len);
    }
    if (std::strcmp(path, "/sdcard") == 0) {
        return tmp_root();
    }
    constexpr const char kPrefixApps[] = "/apps/";
    const size_t apps_len = sizeof(kPrefixApps) - 1;
    if (std::strncmp(path, kPrefixApps, apps_len) == 0) {
        return apps_root() + "/" + (path + apps_len);
    }
    if (std::strcmp(path, "/apps") == 0) {
        return apps_root();
    }
    return path;
}

std::string host_of(const std::string &virtual_path)
{
    return redirect_path(virtual_path.c_str());
}

void unlink_files(const std::initializer_list<const char *> &virtual_paths)
{
    for (const char *path : virtual_paths) {
        ::unlink(host_of(path).c_str());
    }
}

} // namespace hostmock
