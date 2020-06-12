#include <curlpp/Easy.hpp>
#include <curlpp/Infos.hpp>
#include <curlpp/Options.hpp>
#include <curlpp/cURLpp.hpp>

#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

#include <gsl/span>

using U16 = std::uint16_t;
using U32 = std::uint32_t;
using U64 = std::uint64_t;

using F32 = float;
using F64 = float;

using UnixTime = U64;

constexpr std::chrono::duration<F32> RequestInterval{30.0F};

constexpr auto DefaultCommand = "./connectivity-monitor";

constexpr auto TimestampSize = 20;
constexpr auto FirstEpochYear = 70;

constexpr auto FirstSuccessfulHttpStatusCode = 100;
constexpr auto LastSuccessfulHttpStatusCode = 399;

constexpr auto OneHourString = "1H";
constexpr auto OneHourSeconds = 60 * 60;
constexpr auto FourHoursString = "4H";
constexpr auto FourHoursSeconds = 4 * OneHourSeconds;
constexpr auto OneDayString = "1D";
constexpr auto OneDaySeconds = 24 * OneHourSeconds;
constexpr auto OneWeekString = "1W";
constexpr auto OneWeekSeconds = 4 * OneDaySeconds;

constexpr auto DefaultPercentageDigits = 5;
constexpr auto DefaultPercentageStringLength = 3 + 1 + DefaultPercentageDigits + 1;

struct Period {
  std::string name;
  U64 duration;
  Period(std::string name, U64 duration) : name(std::move(name)), duration(duration) {
  }
};

UnixTime unixTimeFromIsoTimestamp(const std::string &timestamp) {
  if (timestamp.size() != TimestampSize) {
    throw std::invalid_argument("Input does not have 20 characters and could not be converted to Unix time.");
  }
  std::tm timeBuffer{};
  std::istringstream ss(timestamp);
  ss >> std::get_time(&timeBuffer, "%Y-%m-%dT%H:%M:%S%z");
  // std::mktime expects local time, so we must offset it to UTC.
  // Find the time_t of epoch, it is 0 on UTC, but timezone elsewhere.
  std::tm epoch{};
  epoch.tm_mday = 1;
  epoch.tm_year = FirstEpochYear;
  // Now we are ready to convert tm to time_t in UTC.
  // This "hack" was taken from https://stackoverflow.com/a/60954178/3271844.
  return std::mktime(&timeBuffer) - std::mktime(&epoch);
}

std::string unixTimeToIsoTimestamp(const UnixTime unixTime) {
  std::array<char, sizeof("1970-01-01T00:00:00Z")> buffer{};
  const auto time = static_cast<std::time_t>(unixTime);
  std::strftime(buffer.data(), buffer.size(), "%FT%TZ", std::gmtime(&time));
  return std::string(buffer.data());
}

std::string getIsoTimestamp() {
  return unixTimeToIsoTimestamp(std::time(nullptr));
}

class Record {
  UnixTime timestamp{};
  std::optional<U16> httpResponseCode{};
  std::optional<U32> microseconds{};

 public:
  explicit Record(UnixTime timestamp) : timestamp(timestamp) {
  }

  [[nodiscard]] UnixTime getTimestamp() const {
    return timestamp;
  }
  void setTimestamp(UnixTime newTimestamp) {
    timestamp = newTimestamp;
  }

  [[nodiscard]] const std::optional<U16> &getHttpResponseCode() const {
    return httpResponseCode;
  }
  void setHttpResponseCode(const std::optional<U16> &newHttpResponseCode) {
    httpResponseCode = newHttpResponseCode;
  }

  [[nodiscard]] const std::optional<U32> &getMicroseconds() const {
    return microseconds;
  }
  void setMicroseconds(const std::optional<U32> &newMicroseconds) {
    microseconds = newMicroseconds;
  }

  void dump(std::ostream &stream) {
    stream << unixTimeToIsoTimestamp(timestamp);
    if (httpResponseCode) {
      stream << ' ' << httpResponseCode.value();
      if (microseconds) {
        stream << ' ' << microseconds.value();
      }
    }
    stream << '\n';
  }
};

Record recordFromString(const std::string &line) {
  std::stringstream stream(line);
  std::string timestamp;
  stream >> timestamp;
  Record record(unixTimeFromIsoTimestamp(timestamp));
  U16 httpResponseCode{};
  if (stream >> httpResponseCode) {
    record.setHttpResponseCode(httpResponseCode);
    U32 microseconds{};
    if (stream >> microseconds) {
      record.setMicroseconds(microseconds);
    }
  }
  return record;
}

std::string padString(const std::string &string, size_t digits) {
  if (string.size() >= digits) {
    return string;
  }
  std::string result;
  size_t required = digits - string.size();
  for (size_t i = 0; i < required; i++) {
    result += ' ';
  }
  result += string;
  return result;
}

std::string toString(double value, int digits) {
  std::stringstream ss;
  ss << std::fixed << std::setprecision(digits) << value;
  return ss.str();
}

U32 getTimeoutInSeconds() {
  return RequestInterval.count() / 2;
}

void run(const std::string &filename, const std::string &url) {
  const auto startingTimePoint = std::chrono::steady_clock::now();
  Record record(std::time(nullptr));
  // Our request to be sent.
  curlpp::Easy request;
  try {
    // Set the URL.
    request.setOpt(curlpp::options::Url(url));
    request.setOpt(curlpp::options::Verbose(false));
    std::stringstream response;
    request.setOpt(curlpp::options::WriteStream(&response));
    request.setOpt(curlpp::options::Timeout(getTimeoutInSeconds()));
    request.perform();
    const auto responseCode = curlpp::infos::ResponseCode::get(request);
    record.setHttpResponseCode(responseCode);
  } catch (curlpp::RuntimeError &e) {
    const auto responseCode = curlpp::infos::ResponseCode::get(request);
    record.setHttpResponseCode(responseCode);
  } catch (curlpp::LogicError &e) {
  }
  const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startingTimePoint);
  record.setMicroseconds(duration.count());
  std::ofstream file(filename, std::ios_base::app);
  record.dump(file);
  file.flush();
}

void printUsage() {
  std::cout << "Use: " << DefaultCommand << " <FILENAME> <ACTION> [URL]" << '\n';
  std::cout << "Actions are --dump, --stats, --monitor <URL>." << '\n';
}

void handleUserInput(std::atomic<bool> &running) {
  std::string inputLine;
  std::cout << "Enter " << '"' << "stop" << '"' << " to stop the application correctly." << '\n' << "> ";
  while (running && std::getline(std::cin, inputLine)) {
    if (inputLine == "stop") {
      std::cout << "The application will stop within the next " << RequestInterval.count() << " second(s)." << '\n';
      running = false;
    } else {
      std::cout << "Unrecognized command." << '\n' << "> ";
    }
  }
}

std::string toPercentageString(const F64 value) {
  return padString(toString(100.0 * value, DefaultPercentageDigits) + "%", DefaultPercentageStringLength);
}

void actionDispatcher(const std::vector<std::string> &arguments) {
  const auto OneHour = Period{OneHourString, OneHourSeconds};
  const auto FourHours = Period{FourHoursString, FourHoursSeconds};
  const auto OneDay = Period{OneDayString, OneDaySeconds};
  const auto OneWeek = Period{OneWeekString, OneWeekSeconds};
  std::vector<Period> periods = {OneHour, FourHours, OneDay, OneWeek};
  if (arguments.size() < 2) {
    printUsage();
    std::exit(EXIT_FAILURE);
  }
  const auto filename = arguments[0];
  const auto action = arguments[1];
  if (action == "--dump") {
    std::ifstream file(filename);
    std::string line;
    while (std::getline(file, line)) {
      const auto record = recordFromString(line);
      std::cout << unixTimeToIsoTimestamp(record.getTimestamp());
      if (record.getHttpResponseCode()) {
        std::cout << ' ' << record.getHttpResponseCode().value();
        if (record.getMicroseconds()) {
          std::cout << ' ' << record.getMicroseconds().value();
        }
      }
      std::cout << '\n';
    }
  }
  if (action == "--stats") {
    std::ifstream file(filename);
    std::string line;
    std::vector<Record> records;
    while (std::getline(file, line)) {
      const auto record = recordFromString(line);
      records.push_back(record);
    }
    std::cout << "Record count: " << std::to_string(records.size()) << '\n';
    for (const auto &period : periods) {
      U64 start = std::time(nullptr) - period.duration;
      U64 periodSamples = period.duration / RequestInterval.count();
      U64 effectiveSamples = 0;
      U64 successes = 0;
      for (auto record : records) {
        if (record.getTimestamp() >= start) {
          effectiveSamples++;
          if (record.getHttpResponseCode()) {
            const auto code = record.getHttpResponseCode().value();
            // This skips any validation of the response code.
            // Maybe not all 1xx, 2xx, 3xx are "successes" for some applications.
            if (code >= FirstSuccessfulHttpStatusCode && code <= LastSuccessfulHttpStatusCode) {
              successes++;
            }
          }
        }
      }
      std::cout << "Coverage (" << period.name << "): " << toPercentageString(effectiveSamples / static_cast<double>(periodSamples)) << '\n';
      std::cout << "Uptime   (" << period.name << "): " << toPercentageString(successes / static_cast<double>(effectiveSamples)) << '\n';
    }
  }
  if (action == "--monitor") {
    if (arguments.size() != 3) {
      printUsage();
      std::exit(EXIT_FAILURE);
    }
    const auto url = arguments[2];
    std::cout << "Monitoring " << url << " and updating " << filename << " every " << RequestInterval.count() << " seconds." << '\n';
    if (getTimeoutInSeconds() != 0U) {
      std::cout << "Requests time-out after " << getTimeoutInSeconds() << " seconds." << '\n';
    }
    std::atomic<bool> running = true;
    std::thread userInputThread(handleUserInput, std::ref(running));
    userInputThread.detach();
    while (running) {
      std::thread helper(run, filename, url);
      helper.detach();
      std::this_thread::sleep_for(RequestInterval);
    }
  }
}

void informAboutException(const std::exception &exception) {
  std::cout << "Threw an exception." << '\n';
  std::cout << "  " << exception.what() << '\n';
}

int main(int argc, char **argv) {
  try {
    // That's all that is needed to do cleanup of used resources (RAII style).
    curlpp::Cleanup cleanup;
    std::vector<std::string> arguments;
    gsl::span<char *, gsl::dynamic_extent> rawArguments(argv, argc);
    for (int i = 1; i < argc; i++) {
      arguments.emplace_back(rawArguments[i]);
    }
    actionDispatcher(arguments);
  } catch (const std::exception &exception) {
    informAboutException(exception);
  }

  return 0;
}
