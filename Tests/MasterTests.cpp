#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DSP/HarmoCore.h"
#include <iostream>
#include <complex>
#include <cstdlib>
#include <new>
#include <bit>
#include <juce_audio_formats/juce_audio_formats.h>

thread_local bool rtRegion = false;
static size_t rtAllocations = 0;
void* operator new(std::size_t n) { if(rtRegion) ++rtAllocations; if(auto* p=std::malloc(n)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }
namespace {
using namespace vitaplug::harmohit;
void check(bool ok,const char* text) { if(!ok) { std::cerr<<"FAIL "<<text<<'\n'; std::exit(1); } }
void param(HarmoHitAudioProcessor& p,const juce::String& id,float value) {
    auto* v=p.parameters.getParameter(id); check(v!=nullptr,"parameter exists");
    v->setValueNotifyingHost(v->convertTo0to1(value));
}
struct Stats { double rms=0,peak=0; };
Stats stats(const std::vector<float>& x) { Stats r; for(float v:x) {check(std::isfinite(v),"finite output"); r.rms+=v*v; r.peak=std::max(r.peak,std::abs(static_cast<double>(v)));} r.rms=std::sqrt(r.rms/static_cast<double>(x.size())); return r; }
double difference(const std::vector<float>& a,const std::vector<float>& b) { double e=0; for(size_t i=0;i<a.size();++i) e=std::max(e,std::abs(static_cast<double>(a[i])-b[i])); return e; }
std::vector<float> input(int frames,double rate,int kind=0) {
    std::vector<float> x(static_cast<size_t>(frames));
    for(int i=0;i<frames;++i) {
        const double t=i/rate;
        x[static_cast<size_t>(i)] = kind==1?(i%2048==0?0.9f:0.0f)
            :kind==2?static_cast<float>(0.9*std::exp(-(i%4096)/180.0)*std::sin(2*juce::MathConstants<double>::pi*90*t)
                +0.15*std::exp(-(i%4096)/15.0)*std::sin(2*juce::MathConstants<double>::pi*4300*t))
            :static_cast<float>(0.8*std::sin(2*juce::MathConstants<double>::pi*997*t));
    } return x;
}
std::vector<float> renderChannel(double rate,float drive,int kind,int stages=2) {
    masterdsp::MasterChannelCore core(stages);
    core.prepare(rate,256,1);
    auto data=input(32768,rate,kind);
    masterdsp::HarmoSettings s; s.drive=drive;
    for(size_t i=0;i<data.size();i+=256) {float* ptr[]{data.data()+i};core.process(ptr,1,256,s);}
    return data;
}
void equivalence() {
    for(double rate:{44100.0,48000.0,96000.0}) for(int mode=0;mode<3;++mode)
    for(const auto amounts : {std::array<float,2>{0,0},std::array<float,2>{0.25f,0},std::array<float,2>{1,0},
                             std::array<float,2>{0,0.2f},std::array<float,2>{0,1},std::array<float,2>{1,1},std::array<float,2>{0.65f,0.4f}}) {
        dsp::HarmoCore reference(dsp::ProductVariant::mix); reference.prepare(rate,256,2);
        masterdsp::MasterEngine master;master.prepare(rate,256);
        auto a=input(8192,rate),b=a,ar=a,br=a;
        dsp::HarmoSettings original{amounts[0],amounts[1],0,0,static_cast<dsp::ExciterMode>(mode)};
        masterdsp::MasterSettings settings;
        settings.banks[0]={amounts[0],amounts[1],0,0,static_cast<masterdsp::ExciterMode>(mode)};
        for(size_t i=0;i<a.size();i+=256) {
            float* ref[]{a.data()+i,ar.data()+i};float* test[]{b.data()+i,br.data()+i};
            reference.process(ref,2,256,original);master.process(test,2,256,settings);
        }
        const double error=difference(a,b);check(error<2e-6,"MIX HARMONICS/EXCITER equivalence");
        std::cout<<"equivalence rate="<<rate<<" mode="<<mode<<" H="<<amounts[0]<<" E="<<amounts[1]<<" max_error="<<error<<'\n';
    }
}
void stateAndEditing() {
    HarmoHitAudioProcessor a,b;
    for(const auto* prefix:{"stereo","mid","side"}) {
        param(a,juce::String(prefix)+".harmonics",juce::String(prefix)=="side"?81:37);
        param(a,juce::String(prefix)+".exciter",22);param(a,juce::String(prefix)+".drive",55);
        param(a,juce::String(prefix)+".range",1);param(a,juce::String(prefix)+".pushAll",1);
    }
    param(a,"processingMode",1);param(a,"editBank",1);param(a,"outputTrim",-6);
    juce::MemoryBlock state;a.getStateInformation(state);b.setStateInformation(state.getData(),static_cast<int>(state.getSize()));
    for(auto* p:a.getParameters()) {
        auto* ranged=dynamic_cast<juce::RangedAudioParameter*>(p);check(ranged!=nullptr,"ranged parameter");
        check(std::bit_cast<uint32_t>(ranged->getValue())==std::bit_cast<uint32_t>(b.parameters.getParameter(ranged->getParameterID())->getValue()),"all parameters round trip");
    }
    a.prepareToPlay(48000,256);b.prepareToPlay(48000,256);
    juce::AudioBuffer<float> aa(2,256),bb(2,256);juce::MidiBuffer midi;
    for(int block=0;block<50;++block) {
        param(a,"editBank",static_cast<float>(block%2));
        for(int c=0;c<2;++c) for(int i=0;i<256;++i) aa.setSample(c,i,0.1f*std::sin(static_cast<float>(block*256+i)*0.13f));
        bb.makeCopyOf(aa);
        rtRegion=true;a.processBlock(aa,midi);b.processBlock(bb,midi);rtRegion=false;
        for(int c=0;c<2;++c) for(int i=0;i<256;++i) check(std::bit_cast<uint32_t>(aa.getSample(c,i))==std::bit_cast<uint32_t>(bb.getSample(c,i)),"edit bank does not alter audio");
    }
    HarmoHitAudioProcessorEditor editor(a);editor.synchroniseControls();
    check(editor.getWidth()==760&&editor.getHeight()==380,"MASTER dimensions");
    param(a,"editBank",1);editor.synchroniseControls();
    auto* knob=dynamic_cast<juce::Slider*>(editor.findChildWithID("harmonics"));
    check(knob&&std::abs(knob->getValue()-81)<0.1,"SIDE attachment initial value");
    knob->setValue(63,juce::sendNotificationSync);
    check(std::abs(a.parameters.getRawParameterValue("side.harmonics")->load()-63)<0.1,"UI updates SIDE only");
    check(std::abs(a.parameters.getRawParameterValue("mid.harmonics")->load()-37)<0.1,"MID preserved");
    param(a,"processingMode",0);editor.synchroniseControls();
    check(!editor.findChildWithID("M")->isEnabled()&&!editor.findChildWithID("S")->isEnabled(),"M/S edit disabled in stereo");
    check(a.getVuUpdateCounter().load()>0&&a.getInputVuDbfs()>-72,"real meter updates");
}
void msAndRealtime() {
    masterdsp::MasterEngine stereo,ms;stereo.prepare(48000,256);ms.prepare(48000,256);
    masterdsp::MasterSettings s;s.midSide=true;
    std::array<std::vector<float>,4> data;for(auto& d:data)d=input(16384,48000,2);
    for(size_t i=0;i<data[0].size();++i) data[1][i]=data[3][i]=data[0][i]*-0.6f;
    for(size_t i=0;i<data[0].size();i+=256) {
        float* a[]{data[0].data()+i,data[1].data()+i};float* b[]{data[2].data()+i,data[3].data()+i};
        stereo.process(a,2,256,{});ms.process(b,2,256,s);
    }
    const double error=std::max(difference(data[0],data[2]),difference(data[1],data[3]));
    check(error<3e-6,"M/S unity vs equal-latency neutral reference");std::cout<<"MS neutral max_error="<<error<<'\n';
    masterdsp::MasterEngine independent;independent.prepare(48000,256);
    s.banks[1].drive=0.6f;s.banks[2].drive=0.2f;
    std::array<float,4096> l{},r{};
    for(int block=0;block<30;++block) {
        for(size_t i=0;i<l.size();++i) {l[i]=0.8f*std::sin(static_cast<float>(i)*0.19f);r[i]=-l[i];}
        s.midSide=block%2==0;s.trimDb=block%3==0?-24.0f:12.0f;
        l[0]=std::numeric_limits<float>::quiet_NaN();r[1]=std::numeric_limits<float>::infinity();
        float* ptr[]{l.data(),r.data()};rtRegion=true;independent.process(ptr,2,4096,s);rtRegion=false;
        for(float v:l)check(std::isfinite(v),"RT finite / large blocks");
    }
    check(rtAllocations==0,"no C++ allocations during process");
    // Mono material: Side must remain zero under independent nonlinear banks.
    masterdsp::MasterEngine mono;mono.prepare(48000,256);s.midSide=true;s.trimDb=0;
    for(int block=0;block<20;++block) {
        for(int i=0;i<256;++i)l[static_cast<size_t>(i)]=r[static_cast<size_t>(i)]=0.2f*std::sin(static_cast<float>(i)*0.1f);
        float* ptr[]{l.data(),r.data()};mono.process(ptr,2,256,s);
        for(int i=0;i<256;++i)check(std::abs(l[static_cast<size_t>(i)]-r[static_cast<size_t>(i)])<1e-6,"mono stability");
    }
}
void independenceTrimAndTransitions() {
    constexpr float scale=0.7071067811865475f;
    double matrixError=0;
    for(int i=0;i<1000;++i) {
        const float l=std::sin(static_cast<float>(i)*0.17f),r=std::cos(static_cast<float>(i)*0.31f);
        const float m=(l+r)*scale,s=(l-r)*scale;
        matrixError=std::max({matrixError,std::abs(static_cast<double>((m+s)*scale-l)),std::abs(static_cast<double>((m-s)*scale-r))});
    }
    check(matrixError<3e-7,"orthonormal matrix inverse");
    masterdsp::MasterEngine a,b;a.prepare(48000,256);b.prepare(48000,256);
    masterdsp::MasterSettings aa,bb;aa.midSide=bb.midSide=true;
    aa.banks[1].drive=0.6f;aa.banks[2].drive=0.2f;bb=aa;bb.banks[1].drive=1;
    std::array<float,256> al{},ar{},bl{},br{};
    double sideError=0,midDifference=0;
    for(int block=0;block<40;++block) {
        for(int i=0;i<256;++i) {
            const float m=0.7f*std::sin(static_cast<float>(block*256+i)*0.13f);
            const float s=0.3f*std::sin(static_cast<float>(block*256+i)*0.19f);
            al[static_cast<size_t>(i)]=bl[static_cast<size_t>(i)]=(m+s)*scale;
            ar[static_cast<size_t>(i)]=br[static_cast<size_t>(i)]=(m-s)*scale;
        }
        float* pa[]{al.data(),ar.data()};float* pb[]{bl.data(),br.data()};
        a.process(pa,2,256,aa);b.process(pb,2,256,bb);
        if(block>10)for(int i=0;i<256;++i) {
            const auto k=static_cast<size_t>(i);
            sideError=std::max(sideError,std::abs(static_cast<double>((al[k]-ar[k])-(bl[k]-br[k]))));
            midDifference=std::max(midDifference,std::abs(static_cast<double>((al[k]+ar[k])-(bl[k]+br[k]))));
        }
    }
    check(sideError<3e-7&&midDifference>0.01,"changing MID DRIVE leaves simultaneous SIDE processing unchanged");
    masterdsp::MasterEngine unity,trimmed;unity.prepare(48000,256);trimmed.prepare(48000,256);
    masterdsp::MasterSettings settings;settings.trimDb=-6;
    double trimError=0;
    for(int block=0;block<40;++block) {
        for(int i=0;i<256;++i) al[static_cast<size_t>(i)]=ar[static_cast<size_t>(i)]=bl[static_cast<size_t>(i)]=br[static_cast<size_t>(i)]=0.1f;
        float* pa[]{al.data(),ar.data()};float* pb[]{bl.data(),br.data()};
        unity.process(pa,2,256,{});trimmed.process(pb,2,256,settings);
        if(block>10)for(int i=0;i<256;++i)trimError=std::max(trimError,std::abs(static_cast<double>(bl[static_cast<size_t>(i)]-al[static_cast<size_t>(i)]*std::pow(10.0f,-6.0f/20.0f))));
    }
    check(trimError<1e-6,"global post-reconstruction trim -6dB");
    masterdsp::MasterEngine transition;transition.prepare(48000,256);
    settings={};settings.banks[1].drive=0.8f;settings.banks[2].drive=0.2f;
    float last=0;double maximumStep=0;
    for(int block=0;block<100;++block) {
        for(int i=0;i<256;++i)al[static_cast<size_t>(i)]=ar[static_cast<size_t>(i)]=0.2f;
        settings.midSide=block>=40&&(block/10)%2==0;
        float* ptr[]{al.data(),ar.data()};transition.process(ptr,2,256,settings);
        for(float v:al){if(block>20)maximumStep=std::max(maximumStep,std::abs(static_cast<double>(v-last)));last=v;}
    }
    check(maximumStep<0.001,"mode crossfade on steady input has no abrupt jump");
    std::cout<<"matrix_error="<<matrixError<<" independent_side_error="<<sideError
             <<" mid_difference="<<midDifference<<" trim_error="<<trimError<<" mode_step="<<maximumStep<<'\n';
}
double binAmplitude(const std::vector<float>& x,int bin,int n=8192) {
    std::complex<double> value{};
    const size_t start=x.size()-static_cast<size_t>(n);
    for(int i=0;i<n;++i)value+=static_cast<double>(x[start+static_cast<size_t>(i)])*
        std::polar(1.0,-2*juce::MathConstants<double>::pi*bin*i/n);
    return std::abs(value)*2/n;
}
void measurements() {
    for(double rate:{44100.0,48000.0,96000.0}) {
        for(int kind=0;kind<3;++kind) for(float drive:{0.0f,0.2f,0.6f,1.0f}) {
            const auto result=renderChannel(rate,drive,kind);const auto measured=stats(result);
            const auto settled=stats(std::vector<float>(result.begin()+8192,result.end()));
            std::cout<<"DRIVE rate="<<rate<<" signal="<<kind<<" control="<<drive<<" rms="<<settled.rms<<" settled_peak="<<settled.peak
                <<" peak_including_startup="<<measured.peak<<" crest="<<settled.peak/settled.rms<<'\n';
            const auto neutral=stats(renderChannel(rate,0,kind));
            // DRIVE 670 R0.2 (approved): POST GAIN raises the output by +1 dB per step by
            // design (no limiter). The old R1 "no overshoot" property no longer applies;
            // compression and colour must keep peaks within POST GAIN + 1 dB of neutral.
            const int step=vitaplug::harmohit::masterdsp::drivecal::StepMapper::nearest(drive);
            check(std::isfinite(measured.peak)&&measured.peak<=neutral.peak*std::pow(10.0,(step+1.0)/20.0)+1e-3,"DRIVE 670 peaks stay within POST GAIN + 1 dB of the neutral path");
        }
        // Coherent high-frequency tone; third harmonic is above Nyquist.
        // Double-precision source phase avoids confusing oscillator leakage with aliasing.
        for(int stages:{2,4}) for(int fundamental:{1511,2003,3001}) for(double level:{0.8,1.131370849898476}) {
            masterdsp::MasterChannelCore core(stages);core.prepare(rate,256,1);
            std::vector<float> signal(32768);constexpr int n=8192;
            for(size_t i=0;i<signal.size();++i)signal[i]=static_cast<float>(level*std::sin(2*juce::MathConstants<double>::pi*fundamental*static_cast<double>(i)/n));
            masterdsp::HarmoSettings s;s.drive=1;
            for(size_t i=0;i<signal.size();i+=256){float* ptr[]{signal.data()+i};core.process(ptr,1,256,s);}
            const auto base=binAmplitude(signal,fundamental),alias=binAmplitude(signal,std::abs(n-3*fundamental));
            const double db=20*std::log10(std::max(alias/base,1e-12));
            std::cout<<"alias rate="<<rate<<" oversampling="<<(1<<stages)<<" f_over_fs="<<static_cast<double>(fundamental)/n<<" level="<<level<<" third_fold_dBc="<<db<<'\n';
            check(std::isfinite(db),"finite alias measurement");
        }
        for(float drive:{0.2f,0.6f,1.0f}) {
            masterdsp::MasterChannelCore core;core.prepare(rate,256,1);
            std::vector<float> signal(32768);constexpr int k=171,n=8192;
            for(size_t i=0;i<signal.size();++i)signal[i]=static_cast<float>(0.8*std::sin(2*juce::MathConstants<double>::pi*k*static_cast<double>(i)/n));
            masterdsp::HarmoSettings s;s.drive=drive;
            for(size_t i=0;i<signal.size();i+=256){float* ptr[]{signal.data()+i};core.process(ptr,1,256,s);}
            const auto amplitude=binAmplitude(signal,k);
            std::cout<<"harmonics rate="<<rate<<" DRIVE="<<drive
                <<" H3_dBc="<<20*std::log10(std::max(binAmplitude(signal,3*k)/amplitude,1e-12))
                <<" H5_dBc="<<20*std::log10(std::max(binAmplitude(signal,5*k)/amplitude,1e-12))<<'\n';
        }
    }
}
void stereoFile(const juce::File& file) {
    juce::AudioFormatManager formats;formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    check(reader&&reader->numChannels>=2,"stereo programme reader");
    HarmoHitAudioProcessor processor;processor.prepareToPlay(reader->sampleRate,256);
    param(processor,"processingMode",1);param(processor,"mid.drive",60);param(processor,"side.drive",20);
    juce::AudioBuffer<float> buffer(2,256);juce::MidiBuffer midi;
    const auto frames=std::min(reader->lengthInSamples,static_cast<juce::int64>(reader->sampleRate*10));
    double inEnergy=0,outEnergy=0,inPeak=0,outPeak=0;
    for(juce::int64 offset=0;offset<frames;offset+=256) {
        const int n=static_cast<int>(std::min<juce::int64>(256,frames-offset));
        buffer.clear();check(reader->read(&buffer,0,n,offset,true,true),"programme read");
        for(int c=0;c<2;++c)for(int i=0;i<n;++i){const auto v=buffer.getSample(c,i);inEnergy+=v*v;inPeak=std::max(inPeak,std::abs(static_cast<double>(v)));}
        rtRegion=true;processor.processBlock(buffer,midi);rtRegion=false;
        for(int c=0;c<2;++c)for(int i=0;i<n;++i){const auto v=buffer.getSample(c,i);check(std::isfinite(v),"stereo programme finite");outEnergy+=v*v;outPeak=std::max(outPeak,std::abs(static_cast<double>(v)));}
    }
    check(rtAllocations==0,"programme process no C++ new");
    std::cout<<"programme="<<file.getFileName()<<" rate="<<reader->sampleRate<<" frames="<<frames
        <<" input_rms="<<std::sqrt(inEnergy/(2*static_cast<double>(frames)))<<" output_rms="<<std::sqrt(outEnergy/(2*static_cast<double>(frames)))
        <<" input_peak="<<inPeak<<" output_peak="<<outPeak<<'\n';
}
}
int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    equivalence();stateAndEditing();msAndRealtime();independenceTrimAndTransitions();measurements();
    if(argc>1)stereoFile(juce::File(juce::String(argv[1])));
    std::cout<<"MASTER checks passed (DRIVE 670 R0.2 integration); physical host smoke remains pending.\n";
}
