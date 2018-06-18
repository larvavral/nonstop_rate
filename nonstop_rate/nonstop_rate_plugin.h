#pragma once

#include <map>
#include <mutex>
#include <random>
#include <string>

#include "log.h"

// This class represent for plugin behavior.
// Only run on history server.
class NonstopRatePlugin : public IMTServerPlugin,
                          public IMTConPluginSink,
                          public IMTTickSink,
                          public IMTConServerSink {
public:
  struct RateInfo {
    double last_bid;
    double last_ask;
    time_t last_real_rate_time;
    time_t last_rate_time;
    int last_rand;
    bool has_real_rate;

    RateInfo() {
      last_bid = 0;
      last_ask = 0;
      last_real_rate_time = 0;
      last_rate_time = 0;
      last_rand = 0;
      has_real_rate = false;
    }
  };

  NonstopRatePlugin(void);
  virtual ~NonstopRatePlugin(void);

  // IMTServerPlugin implementations.
  virtual void Release(void);
  virtual MTAPIRES Start(IMTServerAPI* server);
  virtual MTAPIRES Stop(void);
private:
  // IMTConPluginSink implementations.
  virtual void OnPluginUpdate(const IMTConPlugin* plugin) override;

  // IMTTickSink implementations.
  virtual MTAPIRES HookTick(const int feeder, MTTick& tick) override;

  // IMTConServerSink implementations.
  virtual void OnConServerUpdate(const IMTConServer* server) override;

  // Read plugin parameters.
  void ReadPluginParameters();

  // Read server configuration parameters.
  void ReadServerParameters();

  // Update |RateInfo| of symbols when new tick from main feed came.
  void UpdateRateInfo(const MTTick& tick);

  // Generate fake rate when necessary. It's run on a seperate thread.
  void AddRate();
  // Start/stop add rate thread.
  void StartAddRateThread();
  void StopAddRateThread();

  // Server API.
  IMTServerAPI* server_;

  // Configuration objects.
  IMTConPlugin* plugin_config_;
  IMTConFeeder* feeder_config_;
  IMTConSymbol* symbol_config_;
  IMTConServer* server_config_;

  // Engine which is userd to generate random number.
  std::mt19937 number_engine_;

  // Timeout to add fake rate.
  int timeout_;

  // Feeder name, where we get rate.
  std::wstring feeder_name_;

  // Feeder switch timeout value of history server.
  int feeder_switch_timeout_;

  // All symbols used in Nonstop Rate plugin.
  // Also store it's information to create fake rate.
  using SymbolInformation = std::map<std::wstring, RateInfo>;
  SymbolInformation symbols_;

  // Seperate |AddRate| behavior to another thread.
  std::thread add_rate_thread_;
  // Used to stop add rate thread.
  bool stop_thread_;

  // Mutex to protect behavior of this class.
  // Change to use std::mutex instead of CRITICAL_SECTION because of 
  // performance reason. Since VC140, std::muxtex is faster than CRITICAL_SECTION.
  std::mutex sync_mutex_;
  std::mutex add_rate_mutex_;
};

