#include "BookProcessor.h"
#include "Config.h"
#include "EmbeddedData.h"
#include "Menu.h"

#include <SKSE/SKSE.h>

namespace
{
    void OnSKSEMessage(SKSE::MessagingInterface::Message* message)
    {
        if (!message || message->type != SKSE::MessagingInterface::kDataLoaded) {
            return;
        }

        const auto internalData = VariousBookTags::EmbeddedData::GetInternalData();
        if (internalData.empty()) {
            SKSE::log::critical("Embedded internal-data resource is missing or empty");
        }

        VariousBookTags::Config::GetSingleton().Load(
            internalData,
            "Data/SKSE/Plugins/VariousBookTags_UserConfig.ini",
            "Data/SKSE/Plugins/VariousBookTags_tempCache.ini");
        VariousBookTags::BookProcessor::Apply();
        VariousBookTags::Menu::Register();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
        SKSE::log::critical("Unable to register the SKSE messaging listener");
        return false;
    }

    SKSE::log::info("Various Book Tags initialized");
    return true;
}
