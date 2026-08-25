#include "path_redirect.hpp"

#include <cstdarg>
#include <cstdio>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

/* Wrappers de link (-Wl,--wrap=fopen/open/mkdir/opendir/stat): interceptam
 * os acessos a "/sdcard/..." feitos pelos modulos de producao e redirecionam
 * para o tmpdir, sem alterar uma linha do codigo sob teste. */

extern "C" {

FILE *__real_fopen(const char *path, const char *mode);
int __real_open(const char *path, int flags, ...);
int __real_mkdir(const char *path, mode_t mode);
DIR *__real_opendir(const char *path);
int __real_stat(const char *path, struct stat *buf);

FILE *__wrap_fopen(const char *path, const char *mode)
{
    return __real_fopen(hostmock::redirect_path(path).c_str(), mode);
}

int __wrap_open(const char *path, int flags, ...)
{
    const std::string redirected = hostmock::redirect_path(path);
    if ((flags & O_CREAT) == 0) {
        return __real_open(redirected.c_str(), flags);
    }
    va_list args;
    va_start(args, flags);
    const mode_t mode = static_cast<mode_t>(va_arg(args, int));
    va_end(args);
    return __real_open(redirected.c_str(), flags, mode);
}

int __wrap_mkdir(const char *path, mode_t mode)
{
    return __real_mkdir(hostmock::redirect_path(path).c_str(), mode);
}

DIR *__wrap_opendir(const char *path)
{
    return __real_opendir(hostmock::redirect_path(path).c_str());
}

int __wrap_stat(const char *path, struct stat *buf)
{
    return __real_stat(hostmock::redirect_path(path).c_str(), buf);
}

} // extern "C"
