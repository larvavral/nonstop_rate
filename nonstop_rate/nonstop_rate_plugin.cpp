#include "stdafx.h"
#include "nonstop_rate_plugin.h"

#include <regex>
#include <sstream>

#include "common.h"

namespace {

// Interval time for an 'AddRate' action (milliseconds).
const int kAddRateIntervalTime = 500;

// Default time out value (seconds).
const int kDefaultTimeout = 30;

// Additional data, which used to marked a tick is fake.
const unsigned int kFakeRateReservedBytes[] = { 0x46, 0x41, 0x4B, 0x45 }; // FAKE

bool IsFake(const unsigned int data[]) {
  return std::equal(data, data + 4, kFakeRateReservedBytes);
}

}

NonstopRatePlugin::NonstopRatePlugin(void) {
  // Initialize random number engine.
  std::random_device rd;
  number_engine_.seed(rd());
}


NonstopRatePlugin::~NonstopRatePlugin(void) {
}

void NonstopRatePlugin::Release(void) {
  delete this;
}

MTAPIRES NonstopRatePlugin::Start(IMTServerAPI* server) {
  LogEngine::Journal(INFO, L"Plugin start!");

  // Check version.
  if (!server) return MT_RET_ERR_PARAMS;
  server_ = server;
  
  // Create plugin config.
  if ((plugin_config_ = server_->PluginCreate()) == nullptr ||
      (feeder_config_ = server_->FeederCreate()) == nullptr ||
      (symbol_config_ = server_->SymbolCreate()) == nullptr ||
      (server_config_ = server_->NetServerCreate()) == nullptr) {
    LogEngine::Journal(ERR, L"Creating config objects failed!");
    return MT_RET_ERR_MEM;
  }

  // Read plugin parameters.
  ReadPluginParameters();

  // Read |feeder_switch_timeout_| from server configuration.
  ReadServerParameters();

  // Subscribe for hooks and events.
  MTAPIRES result = MT_RET_OK;
  if ((result = server_->PluginSubscribe(this)) != MT_RET_OK ||
      (result = server_->TickSubscribe(this)) != MT_RET_OK ||
      (result = server_->NetServerSubscribe(this)) != MT_RET_OK) {
    LogEngine::Journal(ERR, L"Subscribing hooks and events failed!");
    return result;
  }

  StartAddRateThread();

  return MT_RET_OK;
}

MTAPIRES NonstopRatePlugin::Stop(void) {
  std::lock_guard<std::mutex> lock(sync_mutex_);
  LogEngine::Journal(INFO, L"Plugin stop!");

  StopAddRateThread();

  // Unsubscribe.
  if (server_) {
    server_->PluginUnsubscribe(this);
    server_->TickUnsubscribe(this);
    server_->NetServerUnsubscribe(this);
  }

  // Clear member variables.
  timeout_ = kDefaultTimeout;
  feeder_name_ = L"";
  symbols_.clear();

  // Delete interface.
  if (plugin_config_) { plugin_config_->Release(); plugin_config_ = nullptr; }
  if (feeder_config_) { feeder_config_->Release(); feeder_config_ = nullptr; }
  if (symbol_config_) { symbol_config_->Release(); symbol_config_ = nullptr; }
  if (server_config_) { server_config_->Release(); server_config_ = nullptr; }

  // Reset server API.
  server_ = nullptr;
  return MT_RET_OK;
}

void NonstopRatePlugin::OnPluginUpdate(const IMTConPlugin* plugin) {
  if (!plugin || !server_ || !plugin_config_)
    return;

#ifdef _DEV
  std::wstringstream ws;
  ws << "OnPluginUpdate(), plugin_name=" << plugin->Name() 
     << ", config_name=" << plugin_config_->Name();
  LogEngine::Journal(INFO, ws.str().c_str());
#endif

  // This event is notified to all plugin, so we need to check plugin name
  // to avoid update unnecessary.
  if (CMTStr::Compare(plugin->Name(), plugin_config_->Name()) == 0 &&
      plugin->Server() == plugin_config_->Server()) {
    ReadPluginParameters();
  }
}

void NonstopRatePlugin::ReadPluginParameters() {
  IMTConParam* param;

  // Initialize for reading parameters process.
  if (server_->PluginCurrent(plugin_config_) != MT_RET_OK) {
    LogEngine::Journal(ERR, L"Get current plugin config failed.");
    return;
  }

  param = server_->PluginParamCreate();
  if (!param) {
    LogEngine::Journal(ERR, L"Create plugin parameter failed.");
    return;
  }

  std::unique_lock<std::mutex> lock(sync_mutex_);
  symbols_.clear();
  feeder_name_ = std::wstring();

  int param_total = plugin_config_->ParameterTotal();
  for (int i = 0; i < param_total; i++) {
    if (plugin_config_->ParameterNext(i, param) != MT_RET_OK)
      continue;

#ifdef _DEV
    std::wstringstream ws;
    ws << "ReadParameters(). Raw data: name=" << param->Name() << ", value=" << param->Value();
    LogEngine::Journal(INFO, ws.str().c_str());
#endif

    if (common::Trim(param->Name()) == std::wstring(TIMEOUT_PARAM_NAME)) {
      // Get 'Timeout' value.
      timeout_ = param->ValueInt();
      if (timeout_ == 0) timeout_ = kDefaultTimeout;
      // Suppose process time is 2s, subtract it from real timeout.
      timeout_ = timeout_ > 2 ? timeout_ - 2 : timeout_;
    } else if (common::Trim(param->Name()) == std::wstring(FEEDER_PARAM_NAME)) {
      // Get 'Feeder' value.
      feeder_name_ = common::Trim(param->ValueString());
    } else {
      // Get 'Symbols' value.
      // Because maximum length of parameter textbox in MT5 is 260 characters.
      // In case of we have too many characters, create new parameter with 
      // the following pattern.
      std::wregex symbol_pattern(L"^\\d{2}\\.Symbols$");
      std::wstring ws = param->Name();
      if (std::regex_match(ws, symbol_pattern)) {
        std::vector<std::wstring> symbol_names = common::Split(param->Value(), L',');
        for (auto& symbol_name : symbol_names)
          symbols_[common::Trim(symbol_name)] = RateInfo();
      }
    }
  }
  lock.unlock();

  // Log all parameters.
  std::wstringstream message;
  message << "ReadParameters(): timeout=" << timeout_
          << ", feeder=" << feeder_name_
          << ", symbols=";
  for (auto const& it : symbols_)
    message << it.first << ",";
  LogEngine::Journal(INFO, message.str());
}

void NonstopRatePlugin::ReadServerParameters() {
  if (server_->NetServerNext(
          IMTConServer::NET_HISTORY_SERVER, server_config_) != MT_RET_OK) {
    LogEngine::Journal(ERR, L"Get server config failed.");
    return;
  }

  feeder_switch_timeout_ = server_config_->HistoryServer()->DatafeedsTimeout();
}

MTAPIRES NonstopRatePlugin::HookTick(const int feeder, MTTick& tick) {
  // Based on value of |feeder|, we can identify the data source.
  //  - The MT_FEEDER_DEALER(-1) value means that the quote was added manually 
  //    through a manager terminal or API.
  //  - A value from 0 to 63 (MT_FEEDER_OFFSET - 1) means that the quote was 
  //    received from a gateway.
  //  - A value of 64 (MT_FEEDER_OFFSET) or greater means that the quote was 
  //    received from a data feed.
  // The MT_FEEDER_DEALER and MT_FEEDER_OFFSET values are defined in 
  // EnMTFeederConstants enum.

  std::wstringstream message;
  // If incoming tick is fake tick, which is generated by this plugin
  // -> update last_rate_time.
  if (feeder == MT_FEEDER_DEALER && IsFake(tick.reserved)) {
    auto it = symbols_.find(tick.symbol);
    if (it != symbols_.end()) {
      std::unique_lock<std::mutex> lock(sync_mutex_);
      it->second.last_rate_time = tick.datetime;
      lock.unlock();
    }

    // Log this tick to file.
    message << "Received fake rate for [" << tick.symbol << "] "
            << "with bid=" << tick.bid << ", "
            << "ask=" << tick.ask << ", "
            << "feeder_index=" << feeder;
    LogEngine::Journal(INFO, message.str().c_str());
    return MT_RET_OK;
  }

  // Do not care about tick from gateway or manually.
  if (feeder < MT_FEEDER_OFFSET) {
    message << "HookTick(). Tick is not from feeder, index=" << feeder;
    LogEngine::Journal(INFO, message.str().c_str());
    return MT_RET_OK;
  }

  std::wstring feeder_name;
  // Get source of tick (data feed name) from feeder index.
  if (server_->FeederNext(feeder - MT_FEEDER_OFFSET, feeder_config_) != MT_RET_OK)
    return MT_RET_OK;
  feeder_name = feeder_config_->Name();

  // Just update if ticks/rates are from main feed.
  if (common::Trim(feeder_name) == feeder_name_)
    UpdateRateInfo(tick);

  return MT_RET_OK;
}

void NonstopRatePlugin::UpdateRateInfo(const MTTick& tick) {
  std::lock_guard<std::mutex> lock(sync_mutex_);

  // Update rate information if tick/rate is in symbols list.
  auto it = symbols_.find(tick.symbol);
  if (it != symbols_.end()) {
    // Only update price if it is real tick/rate.
    if (!IsFake(tick.reserved)) {
      it->second.last_rate_time = tick.datetime;
      it->second.last_real_rate_time = tick.datetime;
      it->second.last_bid = tick.bid;
      it->second.last_ask = tick.ask;
      it->second.has_real_rate = true;

#ifdef _DEV
      //std::wstringstream message;
      //struct tm* time = gmtime(&tick.datetime);
      //message << "Update rate for [" << tick.symbol
      //        << "] at @" << time->tm_hour << ":" << time->tm_min << ":" << time->tm_sec
      //        << ". Bid='" << tick.bid << "', Ask='" << tick.ask;
      //LogEngine::Journal(INFO, message.str());
#endif
    } 
  }
}

void NonstopRatePlugin::OnConServerUpdate(const IMTConServer* server) {
  // This event is notified every time a server configuration is updated.
  // The feeder switch timeout setting is on history server, so no need
  // to care about the other types of server.
  if (server->Type() == IMTConServer::NET_HISTORY_SERVER) {
    std::lock_guard<std::mutex> lock(sync_mutex_);
    LogEngine::Journal(INFO, L"OnConServerUpdate(): Update history server configuration.");
    feeder_switch_timeout_ =
      const_cast<IMTConServer*>(server)->HistoryServer()->DatafeedsTimeout();

#ifdef _DEV
    std::wstringstream ws;
    ws << "OnConServerUpdate(): New feeder switch timeout value is "
      << feeder_switch_timeout_;
    LogEngine::Journal(INFO, ws.str());
#endif
  }
}

void NonstopRatePlugin::AddRate() {
  LogEngine::Journal(INFO, L"AddRate thread start.");

  std::uniform_int_distribution<int> dist(0, 4);
  while (!stop_thread_) {
    // Checking to add fake rate every |kAddRateIntervalTime| milliseconds.
    std::this_thread::sleep_for(std::chrono::milliseconds(kAddRateIntervalTime));

    std::unique_lock<std::mutex> lock(add_rate_mutex_);
    for (auto symbol : symbols_) {
      // Get current time.
      time_t curr_time = server_->TimeCurrent();

      std::wstringstream message;
      // Add fake rate when time is in [time_out_, feeder_switch_timeout).
      // Otherwise, do nothing.
      if (curr_time - symbol.second.last_rate_time < timeout_ ||
          feeder_switch_timeout_ <= curr_time - symbol.second.last_real_rate_time)
        continue;

      // If there are no real rate for this symbol, 
      // do not handle even timeout condition is satisfied.
      if (!symbol.second.has_real_rate) {
        message << "Symbol [" << symbol.first << "] has no real rate. Ignore!";
        LogEngine::Journal(INFO, message.str());
        continue;
      }

      MTTick data { 0 };
      // Fill fake data.
      // Symbol.
      std::copy(symbol.first.begin(), symbol.first.end(), data.symbol);
      // Description.
      std::copy(kFakeRateReservedBytes, kFakeRateReservedBytes + 4, data.reserved);
      // Do not add time for tick, history server will do it for you.
      data.datetime = curr_time;
      // Get symbol config.
      if (server_->SymbolGet(symbol.first.c_str(), symbol_config_) != MT_RET_OK)
        continue;
      // Fake bid/ask.
      int rand;
      int digits = symbol_config_->Digits();
      while ((rand = dist(number_engine_) - 2) == symbol.second.last_rand);
      double offset = rand * std::pow(10, -digits);
      data.bid = symbol.second.last_bid + offset;
      data.ask = symbol.second.last_ask + offset;
      // Save current 'rand' value for future comparing.
      symbols_[symbol.first].last_rand = rand;

      // Change precision to show changing amount of bid/ask in log.
      message.precision(digits + 10);
      message << "Generated fake rate for [" << symbol.first
              << "] with old_bid=" << symbol.second.last_bid
              << ", fake_bid=" << data.bid
              << ", old_ask=" << symbol.second.last_ask
              << ", fake_ask=" << data.ask
              << ", offset=" << offset;
      LogEngine::Journal(INFO, message.str());

      // Add it to price stream.
      server_->TickAdd(data);
    }

    lock.unlock();
  }

  LogEngine::Journal(INFO, L"AddRate thread stop.");
}

void NonstopRatePlugin::StartAddRateThread() {
  stop_thread_ = false;
  add_rate_thread_ = std::thread(&NonstopRatePlugin::AddRate, this);
}

void NonstopRatePlugin::StopAddRateThread() {
  stop_thread_ = true;
  add_rate_thread_.join();
}
