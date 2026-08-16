#include "terminal_cmd.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <vector>

namespace {

// Divide uma linha em tokens respeitando aspas simples e duplas
std::vector<std::string> tokenize(const std::string &line)
{
    std::vector<std::string> tokens;
    std::string current;
    bool in_single_quote = false;
    bool in_double_quote = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
        } else if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
        } else if ((c == ' ' || c == '\t') && !in_single_quote && !in_double_quote) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

// Normaliza um caminho resolvendo '.', '..' e múltiplos slashes
std::string normalize_path(const std::string &path)
{
    std::vector<std::string> parts;
    std::string seg;
    for (size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            if (!seg.empty()) {
                if (seg == "..") {
                    if (!parts.empty()) {
                        parts.pop_back();
                    }
                } else if (seg != ".") {
                    parts.push_back(seg);
                }
                seg.clear();
            }
        } else {
            seg += path[i];
        }
    }

    std::string res;
    for (const auto &p : parts) {
        res += "/" + p;
    }
    return res.empty() ? "/" : res;
}

// Resolve um caminho relativo ao cwd ou absoluto
std::string resolve_path(const std::string &cwd, const std::string &path)
{
    if (path.empty()) {
        return cwd;
    }
    if (path == "~") {
        return "/sdcard";
    }
    if (path[0] == '/') {
        return normalize_path(path);
    }
    return normalize_path(cwd + "/" + path);
}

/* format_size: converte bytes para representacao humana (B/KB/MB). */
/* CodeQL: cpp/unused-static-function - false positive, usada em cmd_ls. */
// NOLINT(misc-unused-using-decls)
std::string format_size(size_t size)
{
    char buf[32];
    if (size < 1024) {
        std::snprintf(buf, sizeof(buf), "%5u B", (unsigned int)size);
    } else if (size < 1024 * 1024) {
        std::snprintf(buf, sizeof(buf), "%5.1f KB", (float)size / 1024.0F);
    } else {
        std::snprintf(buf, sizeof(buf), "%5.1f MB", (float)size / (1024.0F * 1024.0F));
    }
    return std::string(buf);
}

struct DirEntry {
    std::string name;
    bool is_dir;
    size_t size;
};

std::string cmd_ls(const std::vector<std::string> &args, const std::string &cwd)
{
    std::string target_path = cwd;
    if (args.size() > 1) {
        target_path = resolve_path(cwd, args[1]);
    }

    DIR *d = opendir(target_path.c_str());
    if (d == nullptr) {
        return "ls: cannot access '" + target_path + "': " + std::strerror(errno) + "\n";
    }

    std::vector<DirEntry> list;
    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        if (std::strcmp(ent->d_name, ".") == 0 || std::strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        std::string full = target_path + "/" + ent->d_name;
        struct stat st;
        bool is_dir = false;
        size_t sz = 0;
        if (stat(full.c_str(), &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
            sz = (size_t)st.st_size;
        }
        list.push_back({ent->d_name, is_dir, sz});
    }
    closedir(d);

    // Ordena: pastas primeiro, depois por nome alfabético
    std::sort(list.begin(), list.end(), [](const DirEntry &a, const DirEntry &b) {
        if (a.is_dir != b.is_dir) {
            return a.is_dir > b.is_dir;
        }
        return a.name < b.name;
    });

    if (list.empty()) {
        return "(empty directory)\n";
    }

    std::ostringstream out;
    for (const auto &item : list) {
        if (item.is_dir) {
            out << "<DIR>        " << item.name << "/\n";
        } else {
            out << format_size(item.size) << "  " << item.name << "\n";
        }
    }
    return out.str();
}

std::string cmd_cd(const std::vector<std::string> &args, std::string &cwd)
{
    std::string dest = "/sdcard";
    if (args.size() > 1) {
        dest = resolve_path(cwd, args[1]);
    }

    struct stat st;
    if (stat(dest.c_str(), &st) != 0) {
        return "cd: " + dest + ": No such directory\n";
    }
    if (!S_ISDIR(st.st_mode)) {
        return "cd: " + dest + ": Not a directory\n";
    }

    cwd = dest;
    return "";
}

std::string cmd_pwd(const std::string &cwd)
{
    return cwd + "\n";
}

std::string cmd_mkdir(const std::vector<std::string> &args, const std::string &cwd)
{
    if (args.size() < 2) {
        return "mkdir: missing operand\n";
    }

    std::string out;
    for (size_t i = 1; i < args.size(); ++i) {
        std::string target = resolve_path(cwd, args[i]);
        if (mkdir(target.c_str(), 0777) != 0) {
            out += "mkdir: cannot create directory '" + args[i] + "': " + std::strerror(errno) + "\n";
        }
    }
    return out;
}

std::string cmd_rm(const std::vector<std::string> &args, const std::string &cwd)
{
    if (args.size() < 2) {
        return "rm: missing operand\n";
    }

    std::string out;
    for (size_t i = 1; i < args.size(); ++i) {
        std::string target = resolve_path(cwd, args[i]);
        struct stat st;
        if (stat(target.c_str(), &st) != 0) {
            out += "rm: cannot remove '" + args[i] + "': No such file or directory\n";
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (rmdir(target.c_str()) != 0) {
                out += "rm: cannot remove directory '" + args[i] + "': " + std::strerror(errno) + "\n";
            }
        } else {
            if (unlink(target.c_str()) != 0) {
                out += "rm: cannot remove '" + args[i] + "': " + std::strerror(errno) + "\n";
            }
        }
    }
    return out;
}

std::string cmd_rmdir(const std::vector<std::string> &args, const std::string &cwd)
{
    if (args.size() < 2) {
        return "rmdir: missing operand\n";
    }

    std::string out;
    for (size_t i = 1; i < args.size(); ++i) {
        std::string target = resolve_path(cwd, args[i]);
        if (rmdir(target.c_str()) != 0) {
            out += "rmdir: failed to remove '" + args[i] + "': " + std::strerror(errno) + "\n";
        }
    }
    return out;
}

std::string cmd_touch(const std::vector<std::string> &args, const std::string &cwd)
{
    if (args.size() < 2) {
        return "touch: missing file operand\n";
    }

    std::string out;
    for (size_t i = 1; i < args.size(); ++i) {
        std::string target = resolve_path(cwd, args[i]);
        /* FatFS nao suporta permissoes POSIX; fopen("a") e equivalente seguro neste target. */
        /* codeql[cpp/world-writable-file-creation] */
        FILE *f = fopen(target.c_str(), "a"); // NOLINT(android-cloexec-fopen)
        if (f == nullptr) {
            out += "touch: cannot touch '" + args[i] + "': " + std::strerror(errno) + "\n";
        } else {
            fclose(f);
        }
    }
    return out;
}

std::string cmd_cat(const std::vector<std::string> &args, const std::string &cwd)
{
    if (args.size() < 2) {
        return "cat: missing operand\n";
    }

    std::string out;
    for (size_t i = 1; i < args.size(); ++i) {
        std::string target = resolve_path(cwd, args[i]);
        struct stat st;
        if (stat(target.c_str(), &st) != 0) {
            out += "cat: " + args[i] + ": No such file or directory\n";
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            out += "cat: " + args[i] + ": Is a directory\n";
            continue;
        }

        FILE *f = fopen(target.c_str(), "r");
        if (f == nullptr) {
            out += "cat: " + args[i] + ": " + std::strerror(errno) + "\n";
            continue;
        }

        char buf[512];
        size_t total_read = 0;
        const size_t max_bytes = 4096;
        while (total_read < max_bytes) {
            size_t n = fread(buf, 1, sizeof(buf) - 1, f);
            if (n > 0) {
                buf[n] = '\0';
                out.append(buf, n);
                total_read += n;
            }
            if (feof(f)) {
                break; /* fim do arquivo */
            }
            if (ferror(f)) {
                out += "\n[read error]\n";
                break; /* erro de I/O: aborta leitura */
            }
        }
        if (total_read >= max_bytes && !feof(f)) {
            out += "\n[... truncated at 4KB ...]\n";
        }
        fclose(f);
        if (!out.empty() && out.back() != '\n') {
            out += "\n";
        }
    }
    return out;
}

std::string cmd_echo(const std::vector<std::string> &args)
{
    std::string out;
    for (size_t i = 1; i < args.size(); ++i) {
        if (i > 1) {
            out += " ";
        }
        out += args[i];
    }
    out += "\n";
    return out;
}

std::string cmd_help(void)
{
    return "Tab5-OS Shell - Comandos disponíveis:\n"
           "  ls [caminho]      - Lista arquivos e diretórios\n"
           "  cd <caminho>      - Muda o diretório atual\n"
           "  pwd               - Exibe o diretório de trabalho atual\n"
           "  mkdir <dir...>    - Cria novos diretórios\n"
           "  rm <arquivo...>   - Remove arquivos ou diretórios vazios\n"
           "  rmdir <dir...>    - Remove diretórios vazios\n"
           "  touch <arq...>    - Cria arquivos vazios\n"
           "  cat <arquivo...>  - Exibe o conteúdo de arquivos de texto\n"
           "  echo [texto...]   - Imprime texto na tela\n"
           "  clear             - Limpa o terminal\n"
           "  whoami            - Exibe o usuário atual\n"
           "  uname             - Exibe informações do sistema\n"
           "  help              - Mostra esta mensagem de ajuda\n";
}

} // namespace

std::string terminal_exec(const std::string &line, std::string &cwd)
{
    std::vector<std::string> args = tokenize(line);
    if (args.empty()) {
        return "";
    }

    const std::string &cmd = args[0];

    if (cmd == "ls") {
        return cmd_ls(args, cwd);
    }
    if (cmd == "cd") {
        return cmd_cd(args, cwd);
    }
    if (cmd == "pwd") {
        return cmd_pwd(cwd);
    }
    if (cmd == "mkdir") {
        return cmd_mkdir(args, cwd);
    }
    if (cmd == "rm") {
        return cmd_rm(args, cwd);
    }
    if (cmd == "rmdir") {
        return cmd_rmdir(args, cwd);
    }
    if (cmd == "touch") {
        return cmd_touch(args, cwd);
    }
    if (cmd == "cat") {
        return cmd_cat(args, cwd);
    }
    if (cmd == "echo") {
        return cmd_echo(args);
    }
    if (cmd == "clear") {
        return "\x0C"; // Form feed / sinal de clear
    }
    if (cmd == "whoami") {
        return "root@tab5\n";
    }
    if (cmd == "uname") {
        return "Tab5-OS ESP32-P4 FreeRTOS/LVGL9 (RISC-V)\n";
    }
    if (cmd == "help") {
        return cmd_help();
    }

    return "tab5-sh: " + cmd + ": command not found. Digite 'help' para ver os comandos.\n";
}
