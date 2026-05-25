#include "endian.hpp"
#include "fixed_ascii.hpp"
#include "frame_reader.hpp"
#include "message_accessors.hpp"
#include "messages.hpp"
#include "parser.hpp"
#include "types.hpp"

#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct Config {
  std::string input;
  std::string output;
  std::unordered_set<std::string> symbols;
  bool help = false;
};

struct LocateIndex {
  std::unordered_set<itch::u16> target_locates;
  std::unordered_map<std::string, std::vector<itch::u16>> symbol_locates;
  itch::u64 messages = 0;
  itch::u64 directories = 0;
};

struct Stats {
  itch::u64 in_messages = 0;
  itch::u64 out_messages = 0;
  itch::u64 in_bytes = 0;
  itch::u64 out_bytes = 0;
  itch::u64 unknown = 0;
  itch::u64 malformed = 0;
  std::array<itch::u64, 256> in_by_type{};
  std::array<itch::u64, 256> out_by_type{};
};

void usage(const char* argv0) {
  std::cout << "Usage:\n"
            << "  " << argv0
            << " --input <input.bin> --output <output.bin> --symbols <SYM1,SYM2,...>\n\n"
            << "Options:\n"
            << "  --input <path>   Input BinaryFILE-wrapped ITCH file\n"
            << "  --output <path>  Output filtered ITCH file\n"
            << "  --symbols <csv>  Comma-separated symbols, e.g. INTC,AAPL\n"
            << "  --symbol <sym>   Repeatable single symbol option\n"
            << "  -h, --help       Show this help\n";
}

std::string trim(std::string s) {
  const auto begin = s.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }
  const auto end = s.find_last_not_of(" \t\r\n");
  return s.substr(begin, end - begin + 1);
}

std::string upper(std::string s) {
  for (char& c : s) {
    if (c >= 'a' && c <= 'z') {
      c = static_cast<char>(c - 'a' + 'A');
    }
  }
  return s;
}

std::vector<std::string> split_csv(std::string_view input) {
  std::vector<std::string> out;
  std::string current;
  for (char c : input) {
    if (c == ',') {
      out.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  out.push_back(current);
  return out;
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
    if (arg == "--output" && i + 1 < argc) {
      cfg.output = argv[++i];
      continue;
    }
    if (arg == "--symbols" && i + 1 < argc) {
      for (const auto& item : split_csv(argv[++i])) {
        const std::string sym = upper(trim(item));
        if (!sym.empty()) {
          cfg.symbols.insert(sym);
        }
      }
      continue;
    }
    if (arg == "--symbol" && i + 1 < argc) {
      const std::string sym = upper(trim(argv[++i]));
      if (!sym.empty()) {
        cfg.symbols.insert(sym);
      }
      continue;
    }
    std::cerr << "Unknown or incomplete argument: " << arg << "\n";
    return false;
  }

  if (cfg.input.empty()) {
    std::cerr << "--input is required\n";
    return false;
  }
  if (cfg.output.empty()) {
    std::cerr << "--output is required\n";
    return false;
  }
  if (cfg.symbols.empty()) {
    std::cerr << "At least one symbol is required\n";
    return false;
  }
  return true;
}

std::string to_string(itch::Stock stock) {
  return std::string(stock.trimmed());
}

void write_be16(std::ostream& out, itch::u16 value) {
  const char bytes[2] = {
      static_cast<char>((value >> 8) & 0xff),
      static_cast<char>(value & 0xff),
  };
  out.write(bytes, 2);
}

bool is_unknown_or_malformed(const itch::Message& message) {
  return std::holds_alternative<itch::UnknownMessage>(message) ||
         std::holds_alternative<itch::MalformedMessage>(message);
}

bool has_target_k_stock(const itch::Message& message,
                        const std::unordered_set<std::string>& symbols) {
  if (const auto* k = std::get_if<itch::IPOQuotingPeriodUpdate>(&message)) {
    return symbols.find(to_string(k->stock)) != symbols.end();
  }
  return false;
}

LocateIndex build_index(const Config& cfg) {
  LocateIndex index;
  itch::FrameReader reader(cfg.input);
  itch::MessageBodyView view;

  while (reader.next(view)) {
    ++index.messages;
    const itch::Message message = itch::parse_message_body(view);
    if (const auto* directory = std::get_if<itch::StockDirectory>(&message)) {
      ++index.directories;
      const std::string stock = to_string(directory->stock);
      if (cfg.symbols.find(stock) != cfg.symbols.end()) {
        index.target_locates.insert(directory->stock_locate);
        index.symbol_locates[stock].push_back(directory->stock_locate);
      }
    }
  }

  return index;
}

bool should_keep(const itch::Message& message,
                 const LocateIndex& index,
                 const std::unordered_set<std::string>& symbols) {
  if (is_unknown_or_malformed(message)) {
    return true;
  }
  if (has_target_k_stock(message, symbols)) {
    return true;
  }

  const itch::u16 locate = itch::message_stock_locate(message);
  if (locate == 0) {
    return true;
  }
  return index.target_locates.find(locate) != index.target_locates.end();
}

Stats filter_file(const Config& cfg, const LocateIndex& index) {
  itch::FrameReader reader(cfg.input);
  std::ofstream out(cfg.output, std::ios::binary);
  if (!out) {
    throw std::runtime_error("failed to open output file: " + cfg.output);
  }

  Stats stats;
  itch::MessageBodyView view;

  while (reader.next(view)) {
    const itch::Message message = itch::parse_message_body(view);
    const char type = itch::message_type(message);
    ++stats.in_messages;
    stats.in_bytes += static_cast<itch::u64>(2) + view.length;
    ++stats.in_by_type[static_cast<unsigned char>(type)];
    if (std::holds_alternative<itch::UnknownMessage>(message)) {
      ++stats.unknown;
    }
    if (std::holds_alternative<itch::MalformedMessage>(message)) {
      ++stats.malformed;
    }

    if (!should_keep(message, index, cfg.symbols)) {
      continue;
    }

    write_be16(out, view.length);
    out.write(reinterpret_cast<const char*>(view.body), view.length);
    if (!out) {
      throw std::runtime_error("failed while writing output file");
    }
    ++stats.out_messages;
    stats.out_bytes += static_cast<itch::u64>(2) + view.length;
    ++stats.out_by_type[static_cast<unsigned char>(type)];
  }

  return stats;
}

void print_summary(const Config& cfg, const LocateIndex& index, const Stats& stats) {
  std::cout << "Requested symbols:";
  bool first = true;
  for (const auto& symbol : cfg.symbols) {
    std::cout << (first ? " " : ", ") << symbol;
    first = false;
  }
  std::cout << "\n\nResolved locate mapping:\n";
  for (const auto& symbol : cfg.symbols) {
    const auto it = index.symbol_locates.find(symbol);
    if (it == index.symbol_locates.end() || it->second.empty()) {
      std::cerr << "Warning: symbol not found in R messages for this day: "
                << symbol << "\n";
      std::cout << "  " << symbol << " -> (not found)\n";
      continue;
    }
    std::cout << "  " << symbol << " -> ";
    for (std::size_t i = 0; i < it->second.size(); ++i) {
      std::cout << (i == 0 ? "" : ",") << it->second[i];
    }
    std::cout << "\n";
  }

  std::cout << "\nIndex stats:\n"
            << "  messages: " << index.messages << "\n"
            << "  R messages: " << index.directories << "\n"
            << "  target locate count: " << index.target_locates.size() << "\n";

  std::cout << "\nFilter stats:\n"
            << "  messages in: " << stats.in_messages << "\n"
            << "  messages out: " << stats.out_messages << "\n"
            << "  bytes in: " << stats.in_bytes << "\n"
            << "  bytes out: " << stats.out_bytes << "\n"
            << "  unknown messages: " << stats.unknown << "\n"
            << "  malformed messages: " << stats.malformed << "\n";

  std::cout << "\nType counts (in -> out):\n";
  for (std::size_t i = 0; i < stats.in_by_type.size(); ++i) {
    if (stats.in_by_type[i] == 0 && stats.out_by_type[i] == 0) {
      continue;
    }
    const char c = static_cast<char>(i);
    if (c >= 32 && c <= 126) {
      std::cout << "  " << c;
    } else {
      std::cout << "  0x" << std::hex << i << std::dec;
    }
    std::cout << ": " << stats.in_by_type[i] << " -> " << stats.out_by_type[i]
              << "\n";
  }
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
    const LocateIndex index = build_index(cfg);
    const Stats stats = filter_file(cfg, index);
    print_summary(cfg, index, stats);
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
