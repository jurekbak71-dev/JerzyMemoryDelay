#include "control.hpp"
#include "jack_backend.hpp"
#include <string_view>
int main(int argc, char **argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--help") {
    std::cout << "JerzyMultiFXNext JACK2: connect to an already running JACK "
                 "server.\nCommands on stdin; QUIT exits.\n";
    return 0;
  }
  if (argc != 1)
    return 2;
  try {
    nextfx::JackBackend backend;
    return run_control(backend, "JerzyMultiFXNext");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
