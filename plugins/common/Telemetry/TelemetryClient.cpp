#include "TelemetryClient.h"
#include "TelemetrySettings.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace wildjag::Telemetry
{
    namespace
    {
        constexpr const char* kEndpoint = "https://wildjag-telemetry.mr-adambeck.workers.dev/v1/event";
        constexpr int kTimeoutMs = 3000;

        juce::String currentOS()
        {
           #if JUCE_MAC
            return "macOS";
           #elif JUCE_WINDOWS
            return "Windows";
           #elif JUCE_LINUX
            return "Linux";
           #else
            return "Unknown";
           #endif
        }

        juce::String currentHost()
        {
            return juce::PluginHostType().getHostDescription();
        }

        // Constructs the JSON body and hands it to a detached background thread. All of this is
        // wrapped so that any failure - opted out, no install ID, or an exception building the
        // request - just means the event is dropped, never that the caller sees an error.
        void send(juce::DynamicObject::Ptr obj)
        {
            try
            {
                if (TelemetrySettings::isOptedOut())
                    return;

                auto installId = TelemetrySettings::getInstallId();
                if (installId.isEmpty())
                    return;

                obj->setProperty("installId", installId);
                auto json = juce::JSON::toString(juce::var(obj.get()), true);

                juce::Thread::launch([json]
                {
                    try
                    {
                        int statusCode = 0;
                        juce::URL url(kEndpoint);
                        url = url.withPOSTData(json);

                        auto stream = url.createInputStream(
                            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                                .withConnectionTimeoutMs(kTimeoutMs)
                                .withExtraHeaders("Content-Type: application/json")
                                .withStatusCode(&statusCode));

                        // Result intentionally discarded either way - see header comment.
                        juce::ignoreUnused(stream, statusCode);
                    }
                    catch (...)
                    {
                        // Fail silently - see header comment.
                    }
                });
            }
            catch (...)
            {
                // Fail silently - see header comment.
            }
        }
    }

    void sendLaunch(const juce::String& plugin, const juce::String& version)
    {
        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty("type", "launch");
        obj->setProperty("plugin", plugin);
        obj->setProperty("version", version);
        obj->setProperty("os", currentOS());
        obj->setProperty("host", currentHost());
        send(obj);
    }

    void sendPresetLoaded(const juce::String& plugin, const juce::String& version,
                           const juce::String& presetName)
    {
        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty("type", "preset");
        obj->setProperty("plugin", plugin);
        obj->setProperty("version", version);
        obj->setProperty("os", currentOS());
        obj->setProperty("preset", presetName);
        send(obj);
    }
}
