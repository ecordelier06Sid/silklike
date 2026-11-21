#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>

// Exciter harmonique pour voix : isole une bande, génère des harmoniques
// (paires = "Soft", impaires = "Hard"), et les ajoute au signal d'origine.
class SilkLikeProcessor : public juce::AudioProcessor
{
public:
    SilkLikeProcessor()
        : AudioProcessor (BusesProperties()
                              .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "PARAMS", createLayout())
    {
    }

    void prepareToPlay (double sr, int) override
    {
        sampleRate = sr;
        lastLow = lastHigh = -1.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            hp[ch].reset();
            lp[ch].reset();
            dc[ch].reset();
        }
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        auto out = l.getMainOutputChannelSet();
        return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
               && out == l.getMainInputChannelSet();
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;

        const float amount = apvts.getRawParameterValue ("silk")->load();      // 0..1
        const float hard   = apvts.getRawParameterValue ("hard")->load();      // 0 = Soft, 1 = Hard
        float low          = apvts.getRawParameterValue ("low")->load();
        float high         = apvts.getRawParameterValue ("high")->load();

        high = juce::jmin (juce::jmax (high, low * 1.5f), (float) sampleRate * 0.45f);

        if (low != lastLow || high != lastHigh)
        {
            lastLow = low;
            lastHigh = high;
            auto cHp = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, low);
            auto cLp = juce::dsp::IIR::Coefficients<float>::makeLowPass  (sampleRate, high);
            for (int ch = 0; ch < 2; ++ch)
            {
                hp[ch].coefficients = cHp;
                lp[ch].coefficients = cLp;
                dc[ch].coefficients = cHp;
            }
        }

        const int numCh = juce::jmin (buffer.getNumChannels(), 2);
        const int n = buffer.getNumSamples();

        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                const float x = data[i];
                const float band = lp[ch].processSample (hp[ch].processSample (x));

                const float even = 2.0f * band * band;                          // harmonique 2 (doux)
                const float odd  = 4.0f * (std::tanh (3.0f * band) / 3.0f - band); // harmonique 3 (mordant)

                float harm = (1.0f - hard) * even + hard * odd;
                harm = dc[ch].processSample (harm);                              // retire le DC

                data[i] = x + amount * harm;
            }
        }
    }

    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SilkLike Vocal"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        if (auto xml = apvts.copyState().createXml())
            copyXmlToBinary (*xml, dest);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using P = juce::AudioParameterFloat;
        using R = juce::NormalisableRange<float>;
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<P> (juce::ParameterID { "silk", 1 }, "Silk (quantité)", R (0.0f, 1.0f), 0.3f));
        layout.add (std::make_unique<P> (juce::ParameterID { "hard", 1 }, "Soft <-> Hard",   R (0.0f, 1.0f), 0.0f));
        layout.add (std::make_unique<P> (juce::ParameterID { "low",  1 }, "Bande: bas (Hz)",  R (500.0f, 8000.0f, 1.0f, 0.4f), 3000.0f));
        layout.add (std::make_unique<P> (juce::ParameterID { "high", 1 }, "Bande: haut (Hz)", R (4000.0f, 20000.0f, 1.0f, 0.4f), 12000.0f));
        return layout;
    }

    double sampleRate = 44100.0;
    float lastLow = -1.0f, lastHigh = -1.0f;
    std::array<juce::dsp::IIR::Filter<float>, 2> hp, lp, dc;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SilkLikeProcessor)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SilkLikeProcessor();
}
