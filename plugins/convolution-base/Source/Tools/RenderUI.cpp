#include "ConvolutionEditor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdio>
#include <memory>

// Renders the plugin's editor straight to a PNG, with no window, no audio device and no host.
//
// This exists because the alternative - screenshotting the Standalone - needs a real window, and
// macOS re-prompts for microphone access every time the app bundle's signature changes, which a
// rebuild always does. That turns each round of "fix the UI, look at it again" into a manual click,
// which is exactly the loop that most needs to be fast. Painting the component offscreen also
// removes the window chrome, the Standalone's own "audio input is muted" banner, and any
// possibility of capturing the wrong window - so the output can be diffed against the mockup
// directly rather than cropped first.
//
// The juce-hardware-panel-ui skill's verification methodology still applies in full; this just
// replaces its screencapture step with something deterministic.
//
// Usage: ConvBaseRenderUI --out <path.png> [--ir 0] [--predelay 0] [--length 100] [--attack 0]
//                         [--lowcut 20] [--highcut 20000] [--dry 100] [--wet 40]
namespace
{
    std::map<std::string, std::string> parseArgs(int argc, char* argv[])
    {
        std::map<std::string, std::string> args;
        for (int i = 1; i + 1 < argc; i += 2)
        {
            std::string key = argv[i];
            if (key.rfind("--", 0) == 0)
                args[key.substr(2)] = argv[i + 1];
        }
        return args;
    }

    void setParam(wildjag::conv::ConvolutionProcessor& processor, const char* paramID,
                  const std::map<std::string, std::string>& args, const std::string& key)
    {
        const auto it = args.find(key);
        if (it == args.end())
            return;

        if (auto* param = processor.apvts.getParameter(paramID))
            param->setValueNotifyingHost(param->convertTo0to1(std::stof(it->second)));
    }
}

int main(int argc, char* argv[])
{
    using namespace wildjag::conv;

    const auto args = parseArgs(argc, argv);

    const auto outIt = args.find("out");
    if (outIt == args.end())
    {
        std::fprintf(stderr, "Usage: ConvBaseRenderUI --out <path.png> [--ir 0] [--predelay 0]\n"
                             "                        [--length 100] [--attack 0] [--lowcut 20]\n"
                             "                        [--highcut 20000] [--dry 100] [--wet 40]\n");
        return 1;
    }

    // Brings up the font and graphics subsystems (and a MessageManager) without opening a window.
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    ConvolutionProcessor processor(variantConfig());

    setParam(processor, ConvolutionProcessor::irIndexParamID, args, "ir");
    setParam(processor, ConvolutionProcessor::preDelayMsParamID, args, "predelay");
    setParam(processor, ConvolutionProcessor::lengthPercentParamID, args, "length");
    setParam(processor, ConvolutionProcessor::attackMsParamID, args, "attack");
    setParam(processor, ConvolutionProcessor::lowCutHzParamID, args, "lowcut");
    setParam(processor, ConvolutionProcessor::highCutHzParamID, args, "highcut");
    setParam(processor, ConvolutionProcessor::dryParamID, args, "dry");
    setParam(processor, ConvolutionProcessor::wetParamID, args, "wet");

    // prepareToPlay shapes the first IR synchronously and publishes its waveform snapshot, so the
    // display has something real to draw rather than the empty state.
    processor.setPlayConfigDetails(2, 2, 48000.0, 512);
    processor.prepareToPlay(48000.0, 512);

    ConvolutionEditorContent content(processor);

    // Pull the waveform and IR metadata in directly rather than waiting for the editor's 30 Hz
    // timer to fire - a console app has no message loop running, and a render that depended on one
    // would be a race rather than a deterministic tool.
    content.refreshFromProcessor();

    juce::Image image(juce::Image::ARGB, content.getWidth(), content.getHeight(), true);
    {
        juce::Graphics g(image);
        content.paintEntireComponent(g, true);
    }

    const juce::File outFile(juce::File::getCurrentWorkingDirectory().getChildFile(outIt->second));
    outFile.getParentDirectory().createDirectory();
    outFile.deleteFile();

    std::unique_ptr<juce::FileOutputStream> stream(outFile.createOutputStream());
    if (stream == nullptr)
    {
        std::fprintf(stderr, "Could not open %s for writing\n", outFile.getFullPathName().toRawUTF8());
        return 1;
    }

    juce::PNGImageFormat png;
    if (! png.writeImageToStream(image, *stream))
    {
        std::fprintf(stderr, "Could not encode the PNG\n");
        return 1;
    }

    std::printf("wrote %s (%d x %d)\n", outFile.getFullPathName().toRawUTF8(),
                image.getWidth(), image.getHeight());
    return 0;
}
