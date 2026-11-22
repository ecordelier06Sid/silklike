#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cmath>
#include <BinaryData.h>

class SilkLikeEditor;

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
        attCoef = (float) std::exp (-1.0 / (0.002 * sr));
        relCoef = (float) std::exp (-1.0 / (0.060 * sr));
        for (int ch = 0; ch < 2; ++ch)
        {
            env[ch] = 0.0f;
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
        float sumSq = 0.0f;

        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                const float x = data[i];
                const float band = lp[ch].processSample (hp[ch].processSample (x));

                // Enveloppe de la bande, pour que l'effet ne dépende pas du niveau
                const float a = std::abs (band);
                if (a > env[ch]) env[ch] = attCoef * env[ch] + (1.0f - attCoef) * a;
                else             env[ch] = relCoef * env[ch] + (1.0f - relCoef) * a;
                const float nrm = band / (env[ch] + 0.001f);

                constexpr float d = 2.5f;

                // Soft : saturation asymétrique (harmoniques paires + impaires)
                const float soft = (std::tanh (d * nrm + 0.6f) - 0.537f) / d - 0.7116f * nrm;
                // Hard : saturation symétrique (harmoniques impaires)
                const float hardH = std::tanh (d * nrm) / d - nrm;

                float harm = ((1.0f - hard) * soft + hard * hardH) * env[ch];
                harm = dc[ch].processSample (harm);                              // retire le DC

                data[i] = x - 6.0f * std::sqrt (amount) * harm;
                sumSq += data[i] * data[i];
            }
        }

        if (n > 0 && numCh > 0)
            meterRms.store (std::sqrt (sumSq / (float) (n * numCh)));
    }

    juce::AudioProcessorEditor* createEditor() override;
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
    std::atomic<float> meterRms { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using P = juce::AudioParameterFloat;
        using R = juce::NormalisableRange<float>;
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<P> (juce::ParameterID { "silk", 1 }, "Silk (quantite)", R (0.0f, 1.0f), 0.5f));
        layout.add (std::make_unique<P> (juce::ParameterID { "hard", 1 }, "Soft <-> Hard",   R (0.0f, 1.0f), 0.0f));
        layout.add (std::make_unique<P> (juce::ParameterID { "low",  1 }, "Bande: bas (Hz)",  R (300.0f, 8000.0f, 1.0f, 0.4f), 1500.0f));
        layout.add (std::make_unique<P> (juce::ParameterID { "high", 1 }, "Bande: haut (Hz)", R (4000.0f, 20000.0f, 1.0f, 0.4f), 12000.0f));
        return layout;
    }

    double sampleRate = 44100.0;
    float lastLow = -1.0f, lastHigh = -1.0f;
    float attCoef = 0.0f, relCoef = 0.0f;
    float env[2] = { 0.0f, 0.0f };
    std::array<juce::dsp::IIR::Filter<float>, 2> hp, lp, dc;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SilkLikeProcessor)
};

// ---------------------------------------------------------------------------
// Interface : façade, potards en image, vu-mètre à aiguille
// ---------------------------------------------------------------------------
class KnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KnobLookAndFeel()
    {
        knob = juce::ImageCache::getFromMemory (BinaryData::knob_png, BinaryData::knob_pngSize);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float start, float end, juce::Slider&) override
    {
        const float cx = (float) x + (float) w * 0.5f;
        const float cy = (float) y + (float) h * 0.5f;
        const float kr = (float) juce::jmin (w, h) * 0.36f;

        // graduations gravées autour du potard
        for (int i = 0; i <= 10; ++i)
        {
            const float a  = start + (end - start) * (float) i / 10.0f;
            const bool big = (i % 5 == 0);
            const float r1 = kr + 7.0f;
            const float r2 = kr + (big ? 15.0f : 11.0f);
            g.setColour (juce::Colour (0xffb8a890).withAlpha (0.8f));
            g.drawLine (cx + r1 * std::sin (a), cy - r1 * std::cos (a),
                        cx + r2 * std::sin (a), cy - r2 * std::cos (a), big ? 2.0f : 1.2f);
        }

        // ombre portée
        juce::Path disc;
        disc.addEllipse (cx - kr, cy - kr, kr * 2.0f, kr * 2.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.7f), 10, { 2, 5 }).drawForPath (g, disc);

        // potard (l'image est tournée selon la valeur)
        const float angle = start + pos * (end - start);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.saveState();
        g.addTransform (juce::AffineTransform::rotation (angle, cx, cy));
        g.drawImage (knob, juce::Rectangle<float> (cx - kr, cy - kr, kr * 2.0f, kr * 2.0f));
        g.restoreState();
    }

private:
    juce::Image knob;
};

class SilkLikeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SilkLikeEditor (SilkLikeProcessor& p)
        : juce::AudioProcessorEditor (&p), proc (p)
    {
        faceplate = juce::ImageCache::getFromMemory (BinaryData::faceplate_jpg, BinaryData::faceplate_jpgSize);
        meter     = juce::ImageCache::getFromMemory (BinaryData::meter_png, BinaryData::meter_pngSize);
        bezel     = juce::ImageCache::getFromMemory (BinaryData::meter_bezel_png, BinaryData::meter_bezel_pngSize);

        const char* ids[4]   = { "silk", "hard", "low", "high" };
        const char* names[4] = { "SILK", "SOFT / HARD", "BAS", "HAUT" };

        for (int i = 0; i < 4; ++i)
        {
            auto& k = knobs[(size_t) i];
            k.name = names[i];
            k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            k.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            k.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                          juce::MathConstants<float>::pi * 2.75f, true);
            k.slider.setLookAndFeel (&lnf);
            k.slider.onValueChange = [this] { repaint(); };
            addAndMakeVisible (k.slider);
            k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, ids[i], k.slider);
        }

        setSize (600, 448);
        startTimerHz (30);
    }

    ~SilkLikeEditor() override
    {
        for (auto& k : knobs)
            k.slider.setLookAndFeel (nullptr);
    }

    void resized() override
    {
        for (int i = 0; i < 4; ++i)
            knobs[(size_t) i].slider.setBounds (75 + 150 * i - 50, 240, 100, 100);
    }

    void paint (juce::Graphics& g) override
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (faceplate, getLocalBounds().toFloat());

        const auto m = meterBounds();
        g.drawImage (meter, m);

        // aiguille (pivot au bas de la fenêtre du cadran)
        const float pivotX = m.getX() + m.getWidth() * 0.5f;
        const float pivotY = m.getY() + m.getHeight() * 0.6834f;
        const float len    = m.getWidth() * 0.385f;
        const float ang    = juce::degreesToRadians (-55.0f + 110.0f * needle);
        const float tx     = pivotX + len * std::sin (ang);
        const float ty     = pivotY - len * std::cos (ang);

        g.setColour (juce::Colour (0x55200c00));
        g.drawLine (pivotX + 3.0f, pivotY + 3.0f, tx + 3.0f, ty + 4.0f, 2.5f);
        g.setColour (juce::Colour (0xff17120e));
        g.drawLine (pivotX, pivotY, tx, ty, 1.8f);

        // cadre par-dessus l'aiguille
        g.drawImage (bezel, m);

        // titre gravé sur le bas du cadre
        g.setColour (juce::Colour (0xffb9b2a4));
        g.setFont (13.0f);
        g.setFont (g.getCurrentFont().boldened());
        g.drawText ("SILKLIKE VOCAL",
                    juce::Rectangle<float> (m.getX(), m.getY() + m.getHeight() * 0.72f,
                                            m.getWidth(), m.getHeight() * 0.18f),
                    juce::Justification::centred);

        // noms et valeurs des potards
        for (int i = 0; i < 4; ++i)
        {
            const auto b = knobs[(size_t) i].slider.getBounds().toFloat();
            g.setFont (12.0f);
            g.setFont (g.getCurrentFont().boldened());
            g.setColour (juce::Colour (0xffcfc6b4));
            g.drawText (knobs[(size_t) i].name,
                        juce::Rectangle<float> (b.getX() - 20.0f, b.getBottom() - 4.0f, b.getWidth() + 40.0f, 16.0f),
                        juce::Justification::centred);
            g.setColour (juce::Colour (0xffffb347));
            g.drawText (valueText (i),
                        juce::Rectangle<float> (b.getX() - 20.0f, b.getBottom() + 12.0f, b.getWidth() + 40.0f, 16.0f),
                        juce::Justification::centred);
        }
    }

private:
    struct Knob
    {
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
        juce::String name;
    };

    juce::Rectangle<float> meterBounds() const
    {
        const float w = 280.0f;
        const float h = meter.isValid() ? w * (float) meter.getHeight() / (float) meter.getWidth() : 211.0f;
        return { 160.0f, 20.0f, w, h };
    }

    juce::String valueText (int i) const
    {
        const double v = knobs[(size_t) i].slider.getValue();
        if (i < 2)
            return juce::String ((int) std::round (v * 100.0)) + " %";
        if (v >= 1000.0)
            return juce::String (v / 1000.0, 1) + " kHz";
        return juce::String ((int) std::round (v)) + " Hz";
    }

    void timerCallback() override
    {
        const float rms = proc.meterRms.load();
        const float db  = 20.0f * std::log10 (rms + 1.0e-6f);
        const float target = juce::jlimit (0.0f, 1.0f, (db + 40.0f) / 43.0f);
        needle += (target - needle) * (target > needle ? 0.35f : 0.12f);
        repaint (meterBounds().toNearestInt());
    }

    SilkLikeProcessor& proc;
    KnobLookAndFeel lnf;
    std::array<Knob, 4> knobs;
    juce::Image faceplate, meter, bezel;
    float needle = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SilkLikeEditor)
};

juce::AudioProcessorEditor* SilkLikeProcessor::createEditor()
{
    return new SilkLikeEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SilkLikeProcessor();
}
