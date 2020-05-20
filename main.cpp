#include <curlpp/Easy.hpp>
#include <curlpp/Options.hpp>
#include <curlpp/cURLpp.hpp>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

using U16 = std::uint16_t;
using U64 = std::uint64_t;

using F32 = float;

constexpr static std::chrono::duration<F32> RequestInterval{30.0f};

constexpr static auto DefaultCommand = "./connectivity-monitor";

enum class Result : U16 { Undefined = 0, Success = 1, Failure = 2 };

struct Period {
  std::string name;
  U64 duration;
  Period(std::string name, U64 duration) : name(std::move(name)), duration(duration) {}
};

struct Record {
  U64 timestamp{};
  Result result{};
};

std::ostream &operator<<(std::ostream &os, const Record &record) {
  os.write(reinterpret_cast<const char *>(&record.timestamp), sizeof(record.timestamp));
  os.write(reinterpret_cast<const char *>(&record.result), sizeof(record.result));
  return os;
}

std::istream &operator>>(std::istream &is, Record &record) {
  is.read(reinterpret_cast<char *>(&record.timestamp), sizeof(record.timestamp));
  is.read(reinterpret_cast<char *>(&record.result), sizeof(record.result));
  return is;
}

std::string padString(const std::string &string, size_t digits) {
  if (string.size() >= digits) return string;
  std::string result;
  size_t required = digits - string.size();
  for (size_t i = 0; i < required; i++) result += ' ';
  result += string;
  return result;
}

std::string toString(double value, int digits) {
  std::stringstream ss;
  ss << std::fixed << std::setprecision(digits) << value;
  return ss.str();
}

void run(const std::string &filename, const std::string &url) {
  Record record;
  record.timestamp = std::time(nullptr);
  try {
    // That's all that is needed to do cleanup of used resources (RAII style).
    curlpp::Cleanup cleanup;
    // Our request to be sent.
    curlpp::Easy request;
    // Set the URL.
    std::stringstream ss;
    ss << curlpp::options::Url(url);
    record.result = Result::Success;
  } catch (curlpp::RuntimeError &e) {
    record.result = Result::Failure;
  } catch (curlpp::LogicError &e) {
    record.result = Result::Failure;
  }
  std::ofstream file(filename, std::ios::binary | std::ios_base::app);
  file << record;
  file.flush();
}

void printUsage() {
  std::cout << "Use: " << DefaultCommand << " <FILENAME> <ACTION> [URL]" << '\n';
  std::cout << "Actions are --dump, --stats, --monitor <URL>." << '\n';
}

void actionDispatcher(const std::vector<std::string> &arguments) {
  std::vector<Period> periods = {Period{"1H", 60 * 60}, Period{"4H", 4 * 60 * 60}, Period{"1D", 24 * 60 * 60}, Period{"1W", 7 * 24 * 60 * 60}};
  if (arguments.size() < 2) {
    printUsage();
    std::exit(EXIT_FAILURE);
  }
  const auto filename = arguments[0];
  const auto action = arguments[1];
  if (action == "--dump") {
    std::ifstream file(filename, std::ios::binary);
    Record record;
    while (file >> record) {
      std::cout << record.timestamp << ' ' << static_cast<std::underlying_type<Result>::type>(record.result) << '\n';
    }
  }
  if (action == "--stats") {
    std::ifstream file(filename, std::ios::binary);
    std::vector<Record> records;
    {
      Record record;
      while (file >> record) records.push_back(record);
    }
    std::cout << "Records " << padString(std::to_string(records.size()), 16) << '\n';
    for (const auto &period : periods) {
      U64 start = std::time(nullptr) - period.duration;
      U64 periodSamples = period.duration / RequestInterval.count();
      U64 effectiveSamples = 0;
      U64 successes = 0;
      for (auto record : records) {
        if (record.timestamp >= start) {
          effectiveSamples++;
          if (record.result == Result::Success) {
            successes++;
          }
        }
      }
      std::cout << "Coverage (" << period.name << ")  " << padString(toString(100.0 * effectiveSamples / static_cast<double>(periodSamples), 5), 3 + 1 + 5) << '\n';
      std::cout << "Uptime   (" << period.name << ")  " << padString(toString(100.0 * successes / static_cast<double>(effectiveSamples), 5), 3 + 1 + 5) << '\n';
    }
  }
  if (action == "--monitor") {
    if (arguments.size() != 3) {
      printUsage();
      std::exit(EXIT_FAILURE);
    }
    const auto url = arguments[2];
    std::cout << "Monitoring " << url << " and updating " << filename << "." << '\n';
    while (true) {
      std::thread helper(run, filename, url);
      helper.detach();
      std::this_thread::sleep_for(RequestInterval);
    }
  }
}

int main(int argc, char **argv) {
  std::vector<std::string> arguments;
  for (int i = 1; i < argc; i++) {
    arguments.emplace_back(argv[i]);
  }
  actionDispatcher(arguments);
  return 0;
}
