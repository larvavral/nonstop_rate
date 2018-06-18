// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"

#include <string>

#include "common.h"
#include "nonstop_rate_plugin.h"

// Plugin information.
MTPluginInfo plugin_info = {
  100,
  MTServerAPIVersion,
  L"Nonstop Rate Plugin",
  L"",
  L"Plugin to generate fake rate in case feeder do not send rate in time."
};

// Plugin default parameters.
MTPluginParam plugin_default_params[] = {
  { MTPluginParam::TYPE_INT, TIMEOUT_PARAM_NAME, L"30" },
  { MTPluginParam::TYPE_STRING, FEEDER_PARAM_NAME, L"" },
  { MTPluginParam::TYPE_STRING, SYMBOLS_PARAM_NAME, L"" },
};

// DLL entry point.
BOOL APIENTRY DllMain(HMODULE hModule,
                      DWORD ul_reason_for_call,
                      LPVOID lpReserved) {
  switch (ul_reason_for_call) {
  case DLL_PROCESS_ATTACH:
  case DLL_THREAD_ATTACH:
  case DLL_THREAD_DETACH:
  case DLL_PROCESS_DETACH:
    break;
  }

  return TRUE;
}

// Plugin About entry function.
MTAPIENTRY MTAPIRES MTServerAbout(MTPluginInfo& info) {
  info = plugin_info;
  // Copy default parameters value.
  std::memcpy(info.defaults, plugin_default_params, sizeof(plugin_default_params));
  info.defaults_total = _countof(plugin_default_params);
  return(MT_RET_OK);
}

// Plugin instance creation entry point.
MTAPIENTRY MTAPIRES MTServerCreate(UINT apiversion, IMTServerPlugin **plugin) {
  if (!plugin)
    return(MT_RET_ERR_PARAMS);

  if (((*plugin) = new(std::nothrow) NonstopRatePlugin()) == NULL)
    return(MT_RET_ERR_MEM);

  return(MT_RET_OK);
}
