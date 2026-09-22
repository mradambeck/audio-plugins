#include "TelemetrySettings.h"

#include <memory>

namespace wildjag
{
    juce::PropertiesFile* TelemetrySettings::getFile()
    {
        static std::unique_ptr<juce::PropertiesFile> file = []() -> std::unique_ptr<juce::PropertiesFile>
        {
            try
            {
                juce::PropertiesFile::Options options;
                options.applicationName = "WildJag";
                options.filenameSuffix = "properties";
                options.folderName = "WildJag";
                options.osxLibrarySubFolder = "Application Support";
                return std::make_unique<juce::PropertiesFile>(options);
            }
            catch (...)
            {
                return nullptr;
            }
        }();

        return file.get();
    }

    bool TelemetrySettings::isOptedOut()
    {
        auto* file = getFile();
        return file == nullptr || file->getBoolValue("telemetryOptedOut", false);
    }

    juce::String TelemetrySettings::getInstallId()
    {
        auto* file = getFile();
        if (file == nullptr)
            return {};

        auto id = file->getValue("installId");
        if (id.isEmpty())
        {
            id = juce::Uuid().toString();
            file->setValue("installId", id);
            file->saveIfNeeded();
        }
        return id;
    }
}
