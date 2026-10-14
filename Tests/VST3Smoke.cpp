#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_graphics/juce_graphics.h>
#include <iostream>
#include <map>
#include <stdexcept>
#if JUCE_WINDOWS
#include <windows.h>
#endif
void check(bool value, const char* what) { if (!value) throw std::runtime_error(what); }
using Plugin = std::unique_ptr<juce::AudioPluginInstance>;
void set(Plugin& p, const juce::String& name, float value) {
    for (auto* parameter : p->getParameters()) if (parameter->getName(128) == name) { parameter->setValueNotifyingHost(value); return; }
    throw std::runtime_error("missing parameter " + name.toStdString());
}
int main(int argc, char** argv) {
    try {
        juce::ScopedJuceInitialiser_GUI gui; check(argc >= 2, "pass the actual VST3 bundle path");
        juce::VST3PluginFormat format; juce::OwnedArray<juce::PluginDescription> types;
        format.findAllTypesForFile(types, juce::String(argv[1])); check(types.size() == 1, "one VST3 effect in bundle");
        const auto& description = *types[0];
        check(description.name == "VitaPlug HARMOHIT MASTER", "MASTER identity");
        check(description.uniqueId == 250544335, "VST3 stable identifier");
        std::cout << "Loaded version=" << description.version << '\n';
        check(description.version == "0.2.0", "approved R2 version");
        auto create = [&]() { juce::String error; auto plugin = format.createInstanceFromDescription(description, 48000, 256, error); check(plugin != nullptr, error.toRawUTF8()); return plugin; };
        auto a = create(); auto b = create();
        for (const auto* bank : {"stereo", "mid", "side"}) {
            for (const auto* control : {"harmonics", "exciter", "drive", "Range", "Push All"}) {
                const auto name = juce::String(bank) + " " + control;
                bool found = false; for (auto* p : a->getParameters()) if (p->getName(128) == name) found = true;
                check(found, name.toRawUTF8());
            }
        }
        set(a, "stereo drive", 0.7f); set(a, "mid drive", 0.6f); set(a, "side drive", 0.2f);
        set(a, "side harmonics", 0.3f); set(a, "side exciter", 0.4f);
        set(a, "Processing Mode", 1); set(a, "M/S Edit Bank", 1);
        set(a, "Output Trim", 24.0f / 36.0f); set(a, "Bypass", 0);
        juce::MemoryBlock state; a->getStateInformation(state); b->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        check(a->getParameters().size() == b->getParameters().size(), "restored parameter count");
        for (int i = 0; i < a->getParameters().size(); ++i) check(std::abs(a->getParameters()[i]->getValue() - b->getParameters()[i]->getValue()) < 1e-6f, "VST3 state roundtrip");
        set(b, "M/S Edit Bank", 0);
        a->prepareToPlay(48000, 256); b->prepareToPlay(48000, 256);
        check(a->getLatencySamples() == 6 && b->getLatencySamples() == 6, "reported VST3 latency six samples");
        juce::AudioBuffer<float> x(2, 256), y(2, 256); juce::MidiBuffer midi;
        double energy = 0, maximumEditDifference = 0;
        for (int block = 0; block < 80; ++block) {
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) x.setSample(c, i, 0.15f * std::sin((block * 256 + i) * (c == 0 ? 0.13f : 0.19f)));
            y.makeCopyOf(x); a->processBlock(x, midi); b->processBlock(y, midi);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) {
                const auto v = x.getSample(c, i); check(std::isfinite(v), "finite VST3 stereo output"); energy += v * v;
                maximumEditDifference = std::max(maximumEditDifference, std::abs(static_cast<double>(v - y.getSample(c, i))));
            }
        }
        check(energy > 0.01 && maximumEditDifference < 1e-6, "audio passes and edit selection does not change it");
        for (int block = 0; block < 40; ++block) {
            set(a, "Processing Mode", static_cast<float>((block / 5) % 2));
            set(a, "stereo drive", static_cast<float>(block % 11) / 10); set(a, "mid drive", static_cast<float>((block + 4) % 11) / 10);
            set(a, "Output Trim", block % 2 == 0 ? 12.0f / 36.0f : 24.0f / 36.0f);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) x.setSample(c, i, 0.12f * std::sin((block * 256 + i) * 0.09f));
            a->processBlock(x, midi); for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) check(std::isfinite(x.getSample(c, i)), "automated VST3 finite output");
        }
        std::unique_ptr<juce::AudioProcessorEditor> editor(a->createEditorAndMakeActive());
        check(editor && editor->getWidth() == 760 && editor->getHeight() == 380, "actual VST3 editor 760x380");
        juce::DocumentWindow window("MASTER R2 VST3 validation", juce::Colours::black, 0);
        window.setUsingNativeTitleBar(true); window.setContentNonOwned(editor.get(), true); window.centreWithSize(760, 380); window.setVisible(true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(800);
#if JUCE_WINDOWS
        if (argc >= 3) {
            const auto hwnd = static_cast<HWND>(window.getWindowHandle()); RECT rect{}; check(::GetClientRect(hwnd, &rect) != 0, "window client rect");
            const int width = rect.right, height = rect.bottom;
            auto dc = ::GetDC(hwnd); auto memory = ::CreateCompatibleDC(dc);
            BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
            info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB; void* pixels{};
            auto bitmap = ::CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0); check(bitmap && pixels, "capture bitmap");
            auto previous = ::SelectObject(memory, bitmap); check(::BitBlt(memory, 0, 0, width, height, dc, 0, 0, SRCCOPY | CAPTUREBLT) != 0, "capture native VST3 editor");
            juce::Image snapshot(juce::Image::RGB, width, height, true); auto* bytes = static_cast<unsigned char*>(pixels); int green = 0;
            for (int j = 0; j < height; ++j) for (int i = 0; i < width; ++i) {
                const int k = (j * width + i) * 4; const juce::Colour colour(bytes[k + 2], bytes[k + 1], bytes[k]); snapshot.setPixelAt(i, j, colour);
                if (bytes[k + 1] > bytes[k + 2] * 1.3 && bytes[k + 1] > bytes[k] * 1.1 && bytes[k + 1] > 10) ++green;
            }
            ::SelectObject(memory, previous); ::DeleteObject(bitmap); ::DeleteDC(memory); ::ReleaseDC(hwnd, dc);
            auto stream = juce::File(juce::String(argv[2])).createOutputStream(); check(stream != nullptr, "UI capture output"); juce::PNGImageFormat png; check(png.writeImageToStream(snapshot, *stream), "save real Windows UI capture");
            check(green > width * height / 100, "embedded green panel is visible, not missing/black");
            std::cout << "Windows native editor capture " << width << 'x' << height << " green_pixels=" << green << '\n';
        }
#endif
        window.clearContentComponent(); editor.reset(); a->releaseResources(); b->releaseResources();
        std::cout << "PASS actual VST3 identity=" << description.uniqueId << " version=" << description.version
                  << " parameters=" << a->getParameters().size() << " state/audio/mode/automation/trim/editor; edit_error=" << maximumEditDifference << '\n';
        std::cout << "This is a JUCE test host, NOT Pro Tools or Blue Cat PatchWork; NOT the Steinberg validator.\n";
    } catch (const std::exception& e) { std::cerr << "FAIL VST3 smoke: " << e.what() << '\n'; return 1; }
}
