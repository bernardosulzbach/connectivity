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

using U16 = std::uint16_t;
using U64 = std::uint64_t;

static const char *filename = "records.bin";
static const char *url = "http://example.com";
static const U64 samplingInterval = 30;

enum class Result : U16 { Undefined = 0, Success = 1, Failure = 2 };

struct Period {
  std::string name;
  U64 duration;
  Period(std::string name, U64 duration) : name(name), duration(duration) {}
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

std::string padString(std::string string, size_t digits) {
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

int main(int argc, char **argv) {
  std::vector<Period> periods = {Period{"1H", 60 * 60}, Period{"4H", 4 * 60 * 60}, Period{"1D", 24 * 60 * 60}};
  if (argc > 1 && std::string(argv[1]) == "--dump") {
    std::ifstream file(filename, std::ios::binary);
    Record record;
    while (file >> record) {
      std::cout << record.timestamp << ' ' << static_cast<std::underlying_type<Result>::type>(record.result) << '\n';
    }
    return 0;
  }
  if (argc > 1 && std::string(argv[1]) == "--stats") {
    std::ifstream file(filename, std::ios::binary);
    std::vector<Record> records;
    {
      Record record;
      while (file >> record) records.push_back(record);
    }
    std::cout << "Records " << padString(std::to_string(records.size()), 16) << '\n';
    for (auto period : periods) {
      U64 start = std::time(nullptr) - period.duration;
      U64 periodSamples = period.duration / samplingInterval;
      U64 effectiveSamples = 0;
      U64 successes = 0;
      for (auto record : records) {
        if (record.timestamp >= start) {
          effectiveSamples++;
          if (record.result == Result::Success) successes++;
        }
      }
      std::cout << "Coverage (" << period.name << ")  " << padString(toString(100.0 * effectiveSamples / static_cast<double>(periodSamples), 5), 3 + 1 + 5) << '\n';
      std::cout << "Uptime   (" << period.name << ")  " << padString(toString(100.0 * successes / static_cast<double>(effectiveSamples), 5), 3 + 1 + 5) << '\n';
    }
    return 0;
  }
  std::ofstream file(filename, std::ios::binary | std::ios_base::app);
  bool running = true;
  while (running) {
    Record record;
    record.timestamp = std::time(nullptr);
    try {
      // That's all that is needed to do cleanup of used resources (RAII style).
      curlpp::Cleanup cleanup;
      // Our request to be sent.
      curlpp::Easy request;
      std::stringstream ss;
      // Set the URL.
      ss << curlpp::options::Url(url);
      record.result = Result::Success;
    } catch (curlpp::RuntimeError &e) {
      record.result = Result::Failure;
    } catch (curlpp::LogicError &e) {
      record.result = Result::Failure;
    }
    file << record;
    file.flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(samplingInterval * 1000));
  }
  return 0;
}
