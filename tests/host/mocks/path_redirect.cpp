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
        mkdir((base + "/data").c_str(), 0755);
        return base;
    }();
    return root;
}

} // namespace

const std::string &tmp_root()
{
    return make_root();
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
