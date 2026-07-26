#include "BookProcessor.h"
#include "Config.h"
#include "EmbeddedData.h"

#include <SKSE/SKSE.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace
{
    void InitializeLogging()
    {
        const auto directory = SKSE::log::log_directory();
        if (!directory) {
            SKSE::stl::report_and_fail("Unable to locate the SKSE log directory");
        }

        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            (*directory / "VariousBookTags.log").string(), true);
        auto logger = std::make_shared<spdlog::logger>("global", std::move(sink));
#ifndef NDEBUG
        logger->set_level(spdlog::level::debug);
#else
        logger->set_level(spdlog::level::info);
#endif
        logger->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(logger));
    }

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
            internalData, "Data/SKSE/Plugins/VariousBookTags_UserConfig.ini");
        VariousBookTags::BookProcessor::Apply();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    InitializeLogging();
    SKSE::Init(skse);

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
        SKSE::log::critical("Unable to register the SKSE messaging listener");
        return false;
    }

    SKSE::log::info("Various Book Tags initialized");
    return true;
}
