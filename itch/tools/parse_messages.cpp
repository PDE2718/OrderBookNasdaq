#include "formatter.hpp"
#include "frame_reader.hpp"
#include "message_accessors.hpp"
#include "parser.hpp"
#include "types.hpp"

#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {

struct Config {
  std::string input;
  std::string log;
  itch::u64 limit = 2000;
  bool help = false;
};

void usage(const char* argv0) {
  std::cout << "Usage:\n"
            << "  " << argv0 << " --input <file> [--limit 2000] [--log <file>]\n\n"
            << "Options:\n"
            << "  --input <file>   BinaryFILE-wrapped ITCH file\n"
            << "  --limit <n>      Number of messages to parse, default 2000\n"
            << "  --log <file>     Write formatted messages to this file\n"
            << "  -h, --help       Show this help\n";
}

bool parse_args(int argc, char** argv, Config& cfg) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      cfg.help = true;
      return true;
    }
    if (arg == "--input" && i + 1 < argc) {
      cfg.input = argv[++i];
      continue;
    }
    if (arg == "--limit" && i + 1 < argc) {
      cfg.limit = std::strtoull(argv[++i], nullptr, 10);
      continue;
    }
    if (arg == "--log" && i + 1 < argc) {
      cfg.log = argv[++i];
      continue;
    }
    std::cerr << "Unknown or incomplete argument: " << arg << "\n";
    return false;
  }
  if (cfg.input.empty()) {
    std::cerr << "--input is required\n";
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  Config cfg;
  if (!parse_args(argc, argv, cfg)) {
    usage(argv[0]);
    return 2;
  }
  if (cfg.help) {
    usage(argv[0]);
    return 0;
  }

  try {
    itch::FrameReader reader(cfg.input);
    std::ofstream log_file;
    std::ostream* out = &std::cout;
    if (!cfg.log.empty()) {
      log_file.open(cfg.log);
      if (!log_file) {
        std::cerr << "Failed to open log file: " << cfg.log << "\n";
        return 1;
      }
      out = &log_file;
    }

    std::array<itch::u64, 256> counts{};
    itch::MessageBodyView view;
    itch::u64 parsed = 0;
    itch::u64 unknown = 0;
    itch::u64 malformed = 0;

    while (parsed < cfg.limit && reader.next(view)) {
      const itch::Message message = itch::parse_message_body(view);
      const char type = itch::message_type(message);
      ++counts[static_cast<unsigned char>(type)];
      if (std::holds_alternative<itch::UnknownMessage>(message)) {
        ++unknown;
      }
      if (std::holds_alternative<itch::MalformedMessage>(message)) {
        ++malformed;
      }

      *out << itch::format_message(view, message, parsed) << "\n";
      ++parsed;
    }

    std::cerr << "Parsed messages: " << parsed << "\n"
              << "Unknown messages: " << unknown << "\n"
              << "Malformed messages: " << malformed << "\n"
              << "Type counts:\n";
    for (std::size_t i = 0; i < counts.size(); ++i) {
      if (counts[i] == 0) {
        continue;
      }
      const char c = static_cast<char>(i);
      if (std::isprint(static_cast<unsigned char>(c))) {
        std::cerr << "  " << c << ": " << counts[i] << "\n";
      } else {
        std::cerr << "  0x" << std::hex << i << std::dec << ": " << counts[i] << "\n";
      }
    }
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
