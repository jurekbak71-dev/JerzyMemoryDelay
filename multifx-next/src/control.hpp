#pragma once
#include "session.hpp"
#include <csignal>
#include <iostream>
#include <poll.h>
#include <unistd.h>
namespace {
volatile std::sig_atomic_t quit = 0;
void handler(int) { quit = 1; }
} // namespace
template <class Backend> int run_control(Backend &backend, const char *name) {
  try {
    std::signal(SIGINT, handler);
    std::signal(SIGTERM, handler);
    nextfx::Session session(backend.engine());
    backend.start();
    std::cout << "READY " << name << " " << backend.engine().sample_rate()
              << " Hz max_frames=" << backend.engine().max_frames() << '\n'
              << std::flush;
    int result = 0;
    uint32_t reported_latency = 0xffffffffu;
    std::string input;
    bool finished = false;
    while (!quit && !finished && !backend.lost() && !backend.reconfigure()) {
      session.collect();
      auto latency = backend.engine().latency_frames();
      if (latency != reported_latency) {
        backend.update_latency();
        reported_latency = latency;
      }
      pollfd fd{STDIN_FILENO, POLLIN, 0};
      int ready = poll(&fd, 1, 50);
      if (ready <= 0)
        continue;
      if (fd.revents & (POLLIN | POLLHUP)) {
        char buffer[4096];
        ssize_t count = read(STDIN_FILENO, buffer, sizeof buffer);
        if (count <= 0)
          break;
        input.append(buffer, size_t(count));
        for (size_t end; (end = input.find('\n')) != std::string::npos;) {
          std::string line = input.substr(0, end);
          input.erase(0, end + 1);
          if (!line.empty() && line.back() == '\r')
            line.pop_back();
          if (line == "QUIT") {
            std::cout << "OK\n" << std::flush;
            finished = true;
            break;
          }
          if (line.size() > 65536) {
            std::cout << "ERR command too large\n" << std::flush;
            continue;
          }
          auto response = session.command(line);
          if (line == "STATS" && response.rfind("OK ", 0) == 0) {
            auto j = nextfx::Json::parse(response.substr(3));
            j["xruns"] = backend.xruns();
            backend.describe(j);
            response = "OK " + j.dump();
          }
          std::cout << response << '\n' << std::flush;
        }
        if (input.size() > 65536) {
          std::cerr << "control line too large\n";
          result = 2;
          break;
        }
      }
    }
    if (backend.lost()) {
      std::cerr << "Audio backend stopped\n";
      result = 1;
    }
    if (backend.reconfigure()) {
      std::cerr << "Audio configuration changed: restart required\n";
      result = 1;
    }
    backend.stop();
    return result;
  } catch (const std::exception &e) {
    std::cerr << "MultiFX Next: " << e.what() << '\n';
    return 1;
  }
}
