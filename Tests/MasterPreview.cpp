#include "PluginEditor.h"
#include <iostream>
namespace {
class Window final : public juce::DocumentWindow {
public:
    Window(juce::Component* content):DocumentWindow("HARMOHIT MASTER R1",juce::Colours::black,closeButton) {
        setUsingNativeTitleBar(true);setContentOwned(content,true);centreWithSize(760,380);setVisible(true);
    }
    void closeButtonPressed() override {juce::JUCEApplication::getInstance()->quit();}
};
class Preview final : public juce::JUCEApplication,private juce::Timer {
public:
    const juce::String getApplicationName() override{return "HARMOHIT MASTER Preview";}
    const juce::String getApplicationVersion() override{return "R1";}
    void initialise(const juce::String& command) override {
        interactive=command.contains("--interactive");
        processor=std::make_unique<HarmoHitAudioProcessor>();processor->prepareToPlay(48000,512);
        window=std::make_unique<Window>(processor->createEditor());startTimerHz(30);
    }
    void shutdown() override {stopTimer();window.reset();processor.reset();}
private:
    void set(const juce::String& id,float value) {
        auto* p=processor->parameters.getParameter(id);p->setValueNotifyingHost(p->convertTo0to1(value));
    }
    void timerCallback() override {
        juce::AudioBuffer<float> data(2,512);juce::MidiBuffer midi;
        for(int i=0;i<512;++i) {
            const float left=static_cast<float>(0.2*std::sin(phase)),right=static_cast<float>(0.13*std::sin(phase*1.01));
            phase+=juce::MathConstants<double>::twoPi*1000/48000;
            data.setSample(0,i,left);data.setSample(1,i,right);
        }
        processor->processBlock(data,midi);
        if(interactive)return;
        ++ticks;if(ticks%30!=0)return;
        auto* editor=dynamic_cast<HarmoHitAudioProcessorEditor*>(window->getContentComponent());editor->synchroniseControls();
        const auto directory=juce::File(HARMOHIT_PROJECT_DIR).getChildFile("Documentation/visual-checks");directory.createDirectory();
        const auto file=directory.getChildFile("MASTER_R1_live_"+juce::String(ticks/30)+".png");
        juce::FileOutputStream output(file);juce::PNGImageFormat png;
        if(!output.openedOk()||!png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),output)) {
            setApplicationReturnValue(1);quit();return;
        }
        std::cout<<file.getFullPathName()<<'\n';
        if(ticks==30) {
            juce::FileOutputStream retina(directory.getChildFile("MASTER_R1_live_retina.png"));
            png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds(),false,2.0f),retina);
        }
        if(ticks==30) {
            set("processingMode",1);set("mid.harmonics",50);set("mid.exciter",45);set("mid.drive",60);
            set("mid.pushAll",1);set("side.harmonics",25);set("side.exciter",15);set("side.drive",20);
        } else if(ticks==60)set("editBank",1);
        else if(ticks==90)set("processingMode",0);
        else quit();
    }
    std::unique_ptr<HarmoHitAudioProcessor> processor;std::unique_ptr<Window> window;
    double phase=0;int ticks=0;bool interactive=false;
};
}
START_JUCE_APPLICATION(Preview)
