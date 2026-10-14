#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>
int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if(argc!=3)return 1;
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> mixTypes,masterTypes;
    format.findAllTypesForFile(mixTypes,juce::String(argv[1]));
    format.findAllTypesForFile(masterTypes,juce::String(argv[2]));
    if(mixTypes.size()!=1||masterTypes.size()!=1)return 2;
    if(mixTypes[0]->uniqueId==masterTypes[0]->uniqueId)return 3;
    juce::String error;
    auto mix=format.createInstanceFromDescription(*mixTypes[0],48000,256,error);
    if(!mix){std::cerr<<error<<'\n';return 4;}
    auto master=format.createInstanceFromDescription(*masterTypes[0],48000,256,error);
    if(!master){std::cerr<<error<<'\n';return 5;}
    mix->prepareToPlay(48000,256);master->prepareToPlay(48000,256);
    juce::AudioBuffer<float> buffer(2,256);buffer.clear();juce::MidiBuffer midi;
    mix->processBlock(buffer,midi);master->processBlock(buffer,midi);
    std::unique_ptr<juce::AudioProcessorEditor> editor(master->createEditorAndMakeActive());
    if(!editor||editor->getWidth()!=760||editor->getHeight()!=380)return 6;
    std::cout<<"PASS simultaneous VST3 instances: "<<mix->getName()<<" ("<<mixTypes[0]->uniqueId
        <<") and "<<master->getName()<<" ("<<masterTypes[0]->uniqueId<<")\n";
    std::cout<<"MASTER instantiated VST3 editor: 760x380\n";
    editor.reset();mix->releaseResources();master->releaseResources();
    return 0;
}
