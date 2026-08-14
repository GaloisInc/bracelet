#include <CLI/CLI.hpp>
#include <file_handler.hpp>
#include <stdlib.h>
#include <zlib.h>
#ifdef IS_BRACELET_BUILD
#include <snapshot.hpp>
#endif

int main(int argc, char **argv) {
  CLI::App app{"A file handler that performs transformations on a local path"};
  argv = app.ensure_utf8(argv);
  std::filesystem::path target;
  std::filesystem::path magic_path;
  app.add_option("--file", target, "The target file")->required();
  app.add_option("--magic-path", magic_path, "The target file");
  CLI11_PARSE(app, argc, argv);
#ifdef IS_BRACELET_BUILD
  bracelet_snapshot();
#endif
  files::FileHandler fhandler(target, magic_path);
  fhandler.registerCallback<files::ZlibCallback>("application/gzip");
  fhandler.registerCallback<files::XmlCallback>("text/xml");
  fhandler.registerCallback<files::TextCallback>("text/plain");
  fhandler.processFile();

  return EXIT_SUCCESS;
}
