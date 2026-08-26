#pragma once

#include <initializer_list>
#include <string>

namespace hostmock {

/* Raiz temporaria (mkdtemp) que simula o ponto de montagem /sdcard. */
const std::string &tmp_root();

/* Traduz "/sdcard/..." para dentro de tmp_root(); demais caminhos passam
 * intocados. Usada pelos __wrap_* do linker e pelos proprios testes. */
std::string redirect_path(const char *path);

/* Conveniencia para os testes: caminho virtual -> caminho real no host. */
std::string host_of(const std::string &virtual_path);

/* Apaga arquivos virtuais (por exemplo entre casos de teste). */
void unlink_files(const std::initializer_list<const char *> &virtual_paths);

} // namespace hostmock
