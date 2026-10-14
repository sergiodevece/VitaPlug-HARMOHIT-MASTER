#include "PluginEditor.h"
#include "BinaryData.h"
#include "UI/HarmoHitSkinAssets.h"
#include <cmath>
namespace {
using namespace harmohit::ui::embedded;
juce::Image decode(const unsigned char* bytes,size_t count) { return juce::ImageFileFormat::loadFrom(bytes,count); }
const std::array<juce::Rectangle<int>,4> knobBounds{{{213,142,88,88},{336,142,88,88},{459,142,88,88},{616,154,48,48}}};
const std::array<juce::Rectangle<int>,4> modeBounds{{{260,283,49,29},{322,283,49,29},{390,283,49,29},{453,283,49,29}}};
const char* bankNames[]{"stereo","mid","side"};
}
class HarmoHitAudioProcessorEditor::Knob final : public juce::Slider {
public:
    Knob(juce::Image base,juce::Image indicator,bool isTrim=false) : body(base),pointer(indicator),trim(isTrim) {
        setSliderStyle(juce::Slider::RotaryVerticalDrag);
        setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
        setMouseDragSensitivity(220); setWantsKeyboardFocus(true);
        setRotaryParameters(-juce::MathConstants<float>::pi*0.75f,juce::MathConstants<float>::pi*0.75f,true);
    }
    void paint(juce::Graphics& g) override {
        // Opaque recovered body removes the flattened pointer under this knob.
        g.drawImageWithin(body,0,0,getWidth(),getHeight(),juce::RectanglePlacement::stretchToFit);
        const double t=trim ? juce::jlimit(0.0,1.0,(getValue()+12.0)/24.0)
            : (getValue()-getMinimum())/(getMaximum()-getMinimum());
        juce::Graphics::ScopedSaveState save(g);
        g.addTransform(juce::AffineTransform::rotation(juce::degreesToRadians(static_cast<float>(-135+270*t)),
            static_cast<float>(getWidth())*0.5f,static_cast<float>(getHeight())*0.5f));
        g.drawImageWithin(pointer,0,0,getWidth(),getHeight(),juce::RectanglePlacement::stretchToFit);
    }
    void valueChanged() override { setTooltip(getName()+": "+juce::String(getValue(),1)+(trim?" dB":" %")); }
private:
    juce::Image body,pointer; bool trim;
};
class HarmoHitAudioProcessorEditor::ModeButton final : public juce::Button {
public:
    ModeButton(const juce::String& label,juce::Image offImage,juce::Image onImage)
        : Button(label),off(offImage),on(onImage) { setWantsKeyboardFocus(true); }
    void paintButton(juce::Graphics& g,bool,bool) override {
        // Cover the flattened illuminated button completely before dimming.
        g.setOpacity(1.0f);
        g.drawImageWithin(getToggleState()?on:off,0,0,getWidth(),getHeight(),juce::RectanglePlacement::stretchToFit);
        if(!isEnabled()) {g.setColour(juce::Colours::black.withAlpha(0.25f));g.fillRect(getLocalBounds());}
    }
private: juce::Image off,on;
};
class HarmoHitAudioProcessorEditor::ArtButton final : public juce::Button {
public:
    ArtButton(const juce::String& name) : Button(name) { setClickingTogglesState(true); setWantsKeyboardFocus(true); }
    void paintButton(juce::Graphics&,bool,bool) override {}
};
HarmoHitAudioProcessorEditor::HarmoHitAudioProcessorEditor(HarmoHitAudioProcessor& p)
    : AudioProcessorEditor(&p),audioProcessor(p) {
    originalPanel=juce::ImageFileFormat::loadFrom(BinaryData::HarmoHitMasterApproved_png,BinaryData::HarmoHitMasterApproved_pngSize);
    panel=originalPanel.rescaled(760,380,juce::Graphics::highResamplingQuality);
    cleanPanel=decode(panelImage,sizeof(panelImage));
    rangeLever=panel.getClippedImage({74,264,22,25}).createCopy();
    pushTexture=panel.getClippedImage({180,268,11,11}).createCopy();
    const unsigned char* bodies[]{harmonicsBody,exciterBody,driveBody,trimBody};
    const size_t bodySizes[]{sizeof(harmonicsBody),sizeof(exciterBody),sizeof(driveBody),sizeof(trimBody)};
    const unsigned char* pointers[]{harmonicsPointer,exciterPointer,drivePointer,trimPointer};
    const size_t pointerSizes[]{sizeof(harmonicsPointer),sizeof(exciterPointer),sizeof(drivePointer),sizeof(trimPointer)};
    const char* labels[]{"HARMONICS","EXCITER","DRIVE","OUTPUT TRIM"};
    for(size_t i=0;i<4;++i) {
        knobs[i]=std::make_unique<Knob>(decode(bodies[i],bodySizes[i]),decode(pointers[i],pointerSizes[i]),i==3);
        knobs[i]->setName(labels[i]); knobs[i]->setComponentID(i==3?"outputTrim":juce::String(labels[i]).toLowerCase());
        addAndMakeVisible(*knobs[i]);
    }
    attachments[3]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.parameters,"outputTrim",*knobs[3]);
    const auto off=panel.getClippedImage(modeBounds[0]).createCopy();
    const auto on=panel.getClippedImage(modeBounds[1]).createCopy();
    const char* modes[]{"STEREO","M/S","M","S"};
    for(size_t i=0;i<4;++i) {
        modeButtons[i]=std::make_unique<ModeButton>(modes[i],off,on);
        modeButtons[i]->setComponentID(modes[i]);
        modeButtons[i]->onClick=[this,i] {
            setParameter(i<2?"processingMode":"editBank",static_cast<float>(i<2?i:i-2));
            synchroniseControls();
        };
        addAndMakeVisible(*modeButtons[i]);
    }
    rangeButton=std::make_unique<ArtButton>("RANGE 3k/8k");
    pushButton=std::make_unique<ArtButton>("PUSH ALL");
    rangeButton->setComponentID("range"); pushButton->setComponentID("pushAll");
    addAndMakeVisible(*rangeButton); addAndMakeVisible(*pushButton);
    setSize(760,380); setResizable(false,false); synchroniseControls(); startTimerHz(30);
}
HarmoHitAudioProcessorEditor::~HarmoHitAudioProcessorEditor() { stopTimer(); }
void HarmoHitAudioProcessorEditor::setParameter(const juce::String& id,float value) {
    if(auto* p=audioProcessor.parameters.getParameter(id)) {
        p->beginChangeGesture(); p->setValueNotifyingHost(value); p->endChangeGesture();
    }
}
void HarmoHitAudioProcessorEditor::bindBank(int bank) {
    rangeAttachment.reset(); pushAttachment.reset();
    for(size_t i=0;i<3;++i) attachments[i].reset();
    displayedBank=bank;
    const juce::String prefix=juce::String(bankNames[bank])+".";
    const char* ids[]{"harmonics","exciter","drive"};
    for(size_t i=0;i<3;++i)
        attachments[i]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.parameters,prefix+ids[i],*knobs[i]);
    rangeAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(audioProcessor.parameters,prefix+"range",*rangeButton);
    pushAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(audioProcessor.parameters,prefix+"pushAll",*pushButton);
}
void HarmoHitAudioProcessorEditor::synchroniseControls() {
    const bool ms=audioProcessor.parameters.getRawParameterValue("processingMode")->load()>=0.5f;
    const bool side=audioProcessor.parameters.getRawParameterValue("editBank")->load()>=0.5f;
    const int bank=ms?(side?2:1):0;
    if(bank!=displayedBank && !juce::Component::isMouseButtonDownAnywhere()) bindBank(bank);
    for(size_t i=0;i<4;++i) {
        const bool active=i==0?!ms:i==1?ms:i==2?ms&&!side:ms&&side;
        modeButtons[i]->setToggleState(active,juce::dontSendNotification);
        modeButtons[i]->setEnabled(i<2||ms);
        modeButtons[i]->repaint();
    }
    repaint();
}
void HarmoHitAudioProcessorEditor::timerCallback() {
    synchroniseControls();
    displayedInput=juce::jlimit(-20.0f,3.0f,audioProcessor.getInputVuDbfs()+18.0f);
    displayedOutput=juce::jlimit(-20.0f,3.0f,audioProcessor.getOutputVuDbfs()+18.0f);
    repaint();
}
void HarmoHitAudioProcessorEditor::resized() {
    for(size_t i=0;i<4;++i) { knobs[i]->setBounds(knobBounds[i]); modeButtons[i]->setBounds(modeBounds[i]); }
    rangeButton->setBounds(68,255,38,42); pushButton->setBounds(137,256,47,40);
}
void HarmoHitAudioProcessorEditor::paintMeter(juce::Graphics& g,int index,float value) {
    // Reuse clean approved MIX meter faces to cover the flattened MASTER needles.
    const juce::Rectangle<int> rect=index==0?juce::Rectangle<int>(53,130,140,85):juce::Rectangle<int>(562,220,140,85);
    g.drawImage(cleanPanel,rect.getX(),rect.getY(),rect.getWidth(),rect.getHeight(),
        rect.getX(),rect.getY(),rect.getWidth(),rect.getHeight());
    const juce::Point<float> pivot=index==0?juce::Point<float>(123,205.4f):juce::Point<float>(633.3f,295);
    const std::array<juce::Point<float>,4> points=index==0
        ?std::array<juce::Point<float>,4>{{{91.5f,183.1f},{123.8f,171.9f},{148.1f,177.1f},{156.8f,184}}}
        :std::array<juce::Point<float>,4>{{{599.6f,272},{633.2f,260.1f},{656.2f,265.8f},{665.2f,273.4f}}};
    const float ticks[]{-20,-10,0,3};
    int i=1; while(i<3 && value>ticks[i]) ++i;
    const float t=juce::jlimit(0.0f,1.0f,(value-ticks[i-1])/(ticks[i]-ticks[i-1]));
    const auto end=points[static_cast<size_t>(i-1)]+(points[static_cast<size_t>(i)]-points[static_cast<size_t>(i-1)])*t;
    g.setColour(juce::Colour(0xff312719)); g.drawLine(pivot.x,pivot.y,end.x,end.y,1.4f);
}
void HarmoHitAudioProcessorEditor::paint(juce::Graphics& g) {
    g.drawImageWithin(originalPanel,0,0,760,380,juce::RectanglePlacement::stretchToFit);
    paintMeter(g,0,displayedInput); paintMeter(g,1,displayedOutput);
    // Restore only the selector's movable approved parts.
    if(!rangeButton->getToggleState()) {
        // Mirror only the approved lever crop; keep MASTER's printed legends.
        juce::Graphics::ScopedSaveState save(g);
        g.addTransform(juce::AffineTransform::scale(1.0f,-1.0f).translated(74.0f,289.0f));
        g.drawImageAt(rangeLever,0,0);
    }
    if(!pushButton->getToggleState()) {
        // Remove the flattened amber halo with adjacent approved panel texture.
        juce::Graphics::ScopedSaveState save(g);
        juce::Path halo; halo.addEllipse(168.0f,268.0f,11.0f,11.0f);
        g.reduceClipRegion(halo);
        g.drawImageAt(pushTexture,168,268);
        g.setColour(juce::Colour(0xff21180c));g.fillEllipse(170.5f,270.5f,6.0f,6.0f);
        g.setColour(juce::Colour(0xff4a3516));g.drawEllipse(170.5f,270.5f,6.0f,6.0f,0.8f);
    }
}
