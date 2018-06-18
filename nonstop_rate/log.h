#pragma once

#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <string>
#include <tchar.h>

enum Severity {
  INFO = 0,
  WARNING = 1,
  ERR = 2,
  FATAL = 3,
  SERVERITY_NUM
};

// This class used to write log to file.
// We can improve it by adding some kind of logs,
// ex: write log to file, standard output, server log, etc.
// Can use for both std::string and std::wstring.
// But please remember to set |Character Set| property to Unicode or Multi-byte.
// Ex:
//    void foo() {
//      LogEngine::Jounal(INFO, "Hello world!");
//    }
template<typename CharT>
//#ifdef _UNICODE
//         typename = typename std::enable_if<std::is_same<CharT, wchar_t>::value, CharT>::type>
//#else
//         typename = typename std::enable_if<std::is_same<CharT, char>::value, CharT>::type>
//#endif
class Log {
public:
  using StringT = std::basic_string<CharT>;

  static const StringT log_lv[];
  static const StringT kPluginName;
  static const StringT kLogFileExtension;

  // Constructor/ destructor.
  Log() = default;
  virtual ~Log() = default;

  // Log message with serverity.
  static void Journal(Severity serverity, const StringT& message) {
    std::lock_guard<std::mutex> lock(io_mutex_);

    // File handler.
    std::basic_ofstream<CharT> file(GetLogFilePath(), std::ios::app);
    if (!file)
      return;
    // Write log to file as the following format:
    //    "YYYY/mm/dd HH:MM:SS: [serverity]Content of log".
    file << GetCurrentDateTime() << _T(": ")
         << _T("[") << log_lv[serverity] << _T("] ")
         << message << std::endl;
    file.close();
  }

private:
  // Return path of log file.
  static StringT GetLogFilePath() {
    CharT path[256];
    GetModuleFileName(NULL, path, sizeof(path) - 1);
    StringT program_path(path);

    StringT log_file_path = program_path + _T("\\..\\logs\\");
    return log_file_path + kPluginName + _T(".") + GetCurrentDate() + kLogFileExtension;
  }

  // Get time as a string.
  // Format of GetCurrentDate() is "%Y%m%d".
  // Format of GetCurrentDateTime() is "%Y/%m/%d %H:%M%S".
  static StringT GetCurrentDate() {
    using std::chrono::system_clock;
    std::time_t now = system_clock::to_time_t(system_clock::now());

    struct std::tm * ptm = std::localtime(&now);
    const CharT format[] = _T("%Y%m%d");
    std::basic_stringstream<CharT> sstream;
    sstream << std::put_time(ptm, format);
    return sstream.str();
  }

  static StringT GetCurrentDateTime() {
    using std::chrono::system_clock;
    std::time_t now = system_clock::to_time_t(system_clock::now());

    struct std::tm * ptm = std::localtime(&now);
    const CharT format[] = _T("%F %T");
    std::basic_stringstream<CharT> sstream;
    sstream << std::put_time(ptm, format);
    return sstream.str();
  }

  // File handling synchronization.
  static std::mutex io_mutex_;
};

template<typename CharT>
std::mutex Log<CharT>::io_mutex_;

template<typename CharT>
const std::basic_string<CharT> Log<CharT>::log_lv[SERVERITY_NUM] =
    { _T("INFO"), _T("WARNING"), _T("ERROR"), _T("FATAL") };

template<typename CharT>
const std::basic_string<CharT> Log<CharT>::kPluginName = _T("NonstopRate");

template<typename CharT>
const std::basic_string<CharT> Log<CharT>::kLogFileExtension = _T(".log");


#ifdef _UNICODE
using LogEngine = Log<wchar_t>;
#else
using LogEngine = Log<char>;
#endif
