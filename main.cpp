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
using F64 = double;

using UnixTime = U64;

constexpr auto RequestIntervalInMilliseconds = 30000;
constexpr auto TimeoutInMilliseconds = 15000;
// Assumes sleep for is accurate to 1 ms.
// This is important to get the timing loop just right.
constexpr auto SleepForPrecisionInMilliseconds = 10;
// This is used to prevent the CLI from taking too long to terminate.
constexpr auto MaximumSleepForDurationInMilliseconds = 50;

constexpr auto MillisecondsInSecond = 1000;

static_assert(RequestIntervalInMilliseconds % MillisecondsInSecond == 0);
static_assert(TimeoutInMilliseconds % MillisecondsInSecond == 0);

constexpr auto DefaultCommand = "./connectivity-monitor";

constexpr auto Indentation = "  ";

constexpr auto TimestampSize = 20;
constexpr auto FirstEpochYear = 70;

constexpr auto FirstSuccessfulHttpStatusCode = 100;
constexpr auto LastSuccessfulHttpStatusCode = 399;

using SecondCount = U64;

constexpr auto InfiniteSecondCount = std::numeric_limits<SecondCount>::max();

constexpr auto OneHourString = "Last hour";
constexpr SecondCount OneHourSeconds = 60 * 60;
constexpr auto FourHoursString = "Last 4 hours";
constexpr SecondCount FourHoursSeconds = 4 * OneHourSeconds;
constexpr auto OneDayString = "Last day";
constexpr SecondCount OneDaySeconds = 24 * OneHourSeconds;
constexpr auto OneWeekString = "Last week";
constexpr SecondCount OneWeekSeconds = 7 * OneDaySeconds;
constexpr auto ThirtyDaysString = "Last 30 days";
constexpr SecondCount ThirtyDaysSeconds = 30 * OneDaySeconds;
constexpr auto AllTimeString = "All time";
constexpr SecondCount AllTimeSeconds = InfiniteSecondCount;

constexpr auto DefaultPercentageDigits = 5;
constexpr auto DefaultPercentageStringLength = 3 + 1 + DefaultPercentageDigits + 1;

struct Period {
  std::string name;
  SecondCount duration;
  Period(std::string name, SecondCount duration) : name(std::move(name)), duration(duration) {
  }
};

UnixTime getEpochInUnixTime() {
  // std::mktime expects local time, so we must offset it to UTC.
  // Find the time_t of epoch, it is 0 on UTC, but timezone elsewhere.
  std::tm epoch{};
  epoch.tm_mday = 1;
  epoch.tm_year = FirstEpochYear;
  // Now we are ready to convert tm to time_t in UTC.
  // This "hack" was taken from https://stackoverflow.com/a/60954178/3271844.
  return std::mktime(&epoch);
}

UnixTime unixTimeFromIsoTimestamp(const std::string &timestamp) {
  if (timestamp.size() != TimestampSize) {
    throw std::invalid_argument("Input does not have 20 characters and could not be converted to Unix time.");
  }
  std::tm timeBuffer{};
  std::istringstream ss(timestamp);
  ss >> std::get_time(&timeBuffer, "%Y-%m-%dT%H:%M:%S%z");
  return std::mktime(&timeBuffer) - getEpochInUnixTime();
}

std::string unixTimeToIsoTimestamp(const UnixTime unixTime) {
  std::array<char, sizeof("1970-01-01T00:00:00Z")> buffer{};
  const auto time = static_cast<std::time_t>(unixTime);
  std::strftime(buffer.data(), buffer.size(), "%FT%TZ", std::gmtime(&time));
  return std::string(buffer.data());
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

std::string toString(F64 value, int digits) {
  std::stringstream ss;
  ss << std::fixed << std::setprecision(digits) << value;
  return ss.str();
}

void probeUrl(const std::string &filename, const std::string &url) {
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
    request.setOpt(curlpp::options::Timeout(TimeoutInMilliseconds / MillisecondsInSecond));
    request.perform();
    const auto responseCode = curlpp::infos::ResponseCode::get(request);
    record.setHttpResponseCode(responseCode);
  } catch (const curlpp::RuntimeError &e) {
    const auto responseCode = curlpp::infos::ResponseCode::get(request);
    record.setHttpResponseCode(responseCode);
  } catch (const curlpp::LogicError &e) {
  }
  const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startingTimePoint);
  record.setMicroseconds(duration.count());
  std::ofstream file(filename, std::ios_base::app);
  record.dump(file);
}

void printUsage() {
  std::cout << "Use: " << DefaultCommand << " <FILENAME> <ACTION> [URL]" << '\n';
  std::cout << "Actions are --dump, --stats, --monitor <URL>." << '\n';
}

void handleUserInput(std::atomic<bool> &running) {
  std::string inputLine;
  std::cout << "Enter " << '"' << "stop" << '"' << " to stop the application correctly." << '\n' << "> ";
  constexpr auto RequestInterval = std::chrono::milliseconds{RequestIntervalInMilliseconds};
  while (running && std::getline(std::cin, inputLine)) {
    if (inputLine == "stop") {
      running = false;
    } else {
      std::cout << "Unrecognized command." << '\n' << "> ";
    }
  }
}

std::string toPercentageString(const F64 value) {
  return padString(toString(100.0 * value, DefaultPercentageDigits) + "%", DefaultPercentageStringLength);
}

std::vector<Period> getPeriods() {
  const auto OneHour = Period{OneHourString, OneHourSeconds};
  const auto FourHours = Period{FourHoursString, FourHoursSeconds};
  const auto OneDay = Period{OneDayString, OneDaySeconds};
  const auto OneWeek = Period{OneWeekString, OneWeekSeconds};
  const auto ThirtyDays = Period{ThirtyDaysString, ThirtyDaysSeconds};
  const auto AllTime = Period{AllTimeString, AllTimeSeconds};
  return {OneHour, FourHours, OneDay, OneWeek, AllTime};
}

void dumpSamples(const std::string &filename) {
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

void printStatistics(const std::string &filename) {
  U64 recordCount = 0;
  std::ifstream file(filename);
  std::string line;
  auto periods = getPeriods();
  const auto currentTime = std::time(nullptr);
  std::vector<U64> periodStart(periods.size());
  for (std::size_t i = 0; i < periods.size(); i++) {
    if (periods[i].duration != InfiniteSecondCount) {
      periodStart[i] = currentTime - periods[i].duration;
    }
  }
  std::vector<U64> effectiveSamples(periods.size());
  std::vector<U64> successes(periods.size());
  while (std::getline(file, line)) {
    const auto record = recordFromString(line);
    recordCount++;
    for (std::size_t i = 0; i < periods.size(); i++) {
      if (record.getTimestamp() >= periodStart[i]) {
        effectiveSamples[i]++;
        if (record.getHttpResponseCode()) {
          const auto code = record.getHttpResponseCode().value();
          // This skips any validation of the response code.
          // Maybe not all 1xx, 2xx, 3xx are "successes" for some applications.
          if (code >= FirstSuccessfulHttpStatusCode && code <= LastSuccessfulHttpStatusCode) {
            successes[i]++;
          }
        }
      }
    }
  }
  std::cout << "Record count: " << recordCount << '\n';
  for (std::size_t i = 0; i < periods.size(); i++) {
    const auto periodSamples = periods[i].duration / (RequestIntervalInMilliseconds / MillisecondsInSecond);
    std::cout << periods[i].name << '\n';
    if (effectiveSamples[i] == 0) {
      std::cout << Indentation << "No samples" << '\n';
    } else {
      if (periods[i].duration != InfiniteSecondCount) {
        std::cout << Indentation << "Coverage: " << toPercentageString(effectiveSamples[i] / static_cast<F64>(periodSamples)) << '\n';
      }
      std::cout << Indentation << "Uptime:   " << toPercentageString(successes[i] / static_cast<F64>(effectiveSamples[i])) << '\n';
    }
  }
}

void actionDispatcher(const std::vector<std::string> &arguments) {
  if (arguments.size() < 2) {
    printUsage();
    std::exit(EXIT_FAILURE);
  }
  const auto filename = arguments[0];
  const auto action = arguments[1];
  if (action == "--dump") {
    dumpSamples(filename);
  } else if (action == "--stats") {
    printStatistics(filename);
  } else if (action == "--monitor") {
    if (arguments.size() != 3) {
      printUsage();
      std::exit(EXIT_FAILURE);
    }
    const auto url = arguments[2];
    constexpr auto requestIntervalInSeconds = RequestIntervalInMilliseconds / MillisecondsInSecond;
    std::cout << "Monitoring " << url << " and updating " << filename << " every " << requestIntervalInSeconds << " second(s)." << '\n';
    constexpr auto timeoutInSeconds = TimeoutInMilliseconds / MillisecondsInSecond;
    static_assert(timeoutInSeconds != 0);
    std::cout << "Requests time-out after " << timeoutInSeconds << " second(s)." << '\n';
    std::atomic<bool> running = true;
    std::thread userInputThread(handleUserInput, std::ref(running));
    userInputThread.detach();
    const auto requestInterval = std::chrono::milliseconds{RequestIntervalInMilliseconds};
    const auto sleepForPrecision = std::chrono::milliseconds{SleepForPrecisionInMilliseconds};
    const auto maximumSleepForDuration = std::chrono::milliseconds{MaximumSleepForDurationInMilliseconds};
    auto nextProbeLaunch = std::chrono::steady_clock::now();
    while (running) {
      const auto timeForLaunch = nextProbeLaunch - std::chrono::steady_clock::now();
      // Start by sleeping.
      if (timeForLaunch > sleepForPrecision) {
        // Don't sleep for more than MaximumSleepForDuration.
        if (timeForLaunch - sleepForPrecision > maximumSleepForDuration) {
          std::this_thread::sleep_for(maximumSleepForDuration);
        } else {
          std::this_thread::sleep_for(timeForLaunch - sleepForPrecision);
        }
      } else {
        // Then busy-wait (spin lock).
        while (nextProbeLaunch - std::chrono::steady_clock::now() > std::chrono::milliseconds{0}) {
        }
        std::thread probeThread(probeUrl, filename, url);
        probeThread.detach();
        nextProbeLaunch = nextProbeLaunch + requestInterval;
      }
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
