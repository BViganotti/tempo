#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <string_view>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>

#include "tempo/assets.hpp"
#include "tempo/http.hpp"
#include "tempo/version.hpp"
#include "tempo/workbench.hpp"

namespace {

std::atomic<bool> g_stop{false};

void on_signal(int) {
  g_stop = true;
}

struct Args {
  std::string data_dir;
  std::uint16_t port = 0;
  bool open_browser = true;
};

void print_usage() {
  std::fprintf(stderr,
               "Tempo %s\n"
               "usage: tempo_workbench [--data-dir PATH] [--port N] [--no-open]\n"
               "\n"
               "Starts the local workbench on 127.0.0.1 and opens the default\n"
               "browser. There is no terminal workflow; press Ctrl+C to stop.\n",
               std::string(tempo::kVersion).c_str());
}

bool parse_args(int argc, char** argv, Args* args, std::string* error) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      print_usage();
      std::exit(0);
    }
    if (arg == "--no-open") {
      args->open_browser = false;
      continue;
    }
    if (arg == "--data-dir") {
      if (i + 1 >= argc) {
        *error = "--data-dir requires a value";
        return false;
      }
      args->data_dir = argv[++i];
      continue;
    }
    if (arg == "--port") {
      if (i + 1 >= argc) {
        *error = "--port requires a value";
        return false;
      }
      char* end = nullptr;
      const long value = std::strtol(argv[++i], &end, 10);
      if (end == nullptr || *end != '\0' || value < 0 || value > 65535) {
        *error = "--port must be 0..65535";
        return false;
      }
      args->port = static_cast<std::uint16_t>(value);
      continue;
    }
    *error = "unknown argument: " + std::string(arg);
    return false;
  }
  return true;
}

std::string mint_token() {
  std::string token;
  token.reserve(32);
  std::random_device device;
  const char* hex = "0123456789abcdef";
  for (int i = 0; i < 16; ++i) {
    const std::uint32_t value = device();
    token.push_back(hex[(value >> 4) & 0xF]);
    token.push_back(hex[value & 0xF]);
  }
  return token;
}

void open_browser(const std::string& url) {
#if defined(__APPLE__)
  const pid_t pid = ::fork();
  if (pid == 0) {
    ::execl("/usr/bin/open", "open", url.c_str(), static_cast<char*>(nullptr));
    ::_exit(127);
  }
  if (pid > 0) {
    int status = 0;
    (void)::waitpid(pid, &status, 0);
  }
#else
  (void)url;
#endif
}

}  // namespace

int main(int argc, char** argv) {
  Args args;
  std::string error;
  if (!parse_args(argc, argv, &args, &error)) {
    std::fprintf(stderr, "tempo: %s\n", error.c_str());
    print_usage();
    return 2;
  }

  tempo::WorkbenchOptions workbench_options;
  workbench_options.data_dir = args.data_dir;
  tempo::Workbench workbench(workbench_options);
  if (!workbench.initialize(&error)) {
    std::fprintf(stderr, "tempo: %s\n", error.c_str());
    return 1;
  }

  tempo::HttpServerOptions server_options;
  server_options.port = args.port;
  server_options.token = mint_token();
  tempo::HttpServer server(server_options,
                           [&workbench](const tempo::HttpRequest& request) {
                             return workbench.handle(request);
                           });
  workbench.set_broadcaster(
      [&server](std::string_view event, std::string_view data) {
        server.broadcast_sse(event, data);
      });
  if (!server.start(&error)) {
    std::fprintf(stderr, "tempo: %s\n", error.c_str());
    return 1;
  }

  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);
  std::signal(SIGPIPE, SIG_IGN);

  const std::string url = server.url();
  std::fprintf(stderr,
               "Tempo %s — local workbench\n"
               "  UI:   %s\n"
               "  data: %s\n"
               "Press Ctrl+C to stop. No terminal interaction is required.\n",
               std::string(tempo::kVersion).c_str(), url.c_str(),
               workbench.data_dir().c_str());
  if (args.open_browser) {
    open_browser(url);
  }
  server.run([] { return g_stop.load(); });
  server.stop();
  std::fprintf(stderr, "Tempo stopped.\n");
  return 0;
}
