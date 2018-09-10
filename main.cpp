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

enum class Result : U16 { Undefined = 0, Success = 1, Failure = 2 };

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

std::string to_string(double value, int digits) {
  std::stringstream ss;
  ss << std::fixed << std::setprecision(digits) << value;
  return ss.str();
}

int main(int argc, char **argv) {
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
    Record record;
    U64 records = 0;
    U64 successes = 0;
    while (file >> record) {
      records++;
      if (record.result == Result::Success) successes++;
    }
    std::cout << "Records   " << records << '\n';
    std::cout << "Uptime    " << to_string(100.0 * successes / static_cast<double>(records), 3) << '%' << '\n';
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
    std::this_thread::sleep_for(std::chrono::milliseconds(30 * 1000));
  }
  return 0;
}
