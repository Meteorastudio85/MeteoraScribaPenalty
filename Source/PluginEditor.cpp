#include "PluginEditor.h"

namespace
{
    const juce::Colour bgColour     { 0xff131519 };
    const juce::Colour panelColour  { 0xff1d2027 };
    const juce::Colour lineColour   { 0xff2c313a };
    const juce::Colour textColour   { 0xffe8e8ea };
    const juce::Colour dimColour    { 0xff8a909b };
    const juce::Colour downColour   { 0xffff6b5e };
    const juce::Colour upColour     { 0xff4fd18b };

    juce::Font makeFont (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions().withHeight (height).withStyle (bold ? "Bold" : "Regular"));
    }

    juce::String formatLufs (float v)
    {
        return v > -69.95f ? juce::String (v, 1) : juce::String ("--.-");
    }

    juce::String formatSigned (float v)
    {
        if (std::abs (v) < 0.05f) return "0.0";
        return (v > 0.0f ? "+" : "") + juce::String (v, 1);
    }

    juce::String formatTime (float seconds)
    {
        const int s = (int) seconds;
        return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }
}

//==============================================================================
ScribaPenaltyAudioProcessorEditor::ScribaPenaltyAudioProcessorEditor (ScribaPenaltyAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    history.fill (-100.0f);

    platformBox.addItemList (scriba::platformNames(), 1);
    sourceBox.addItemList ({ "Integrato (brano intero)", "Short-term (live 3 s)" }, 1);

    for (auto* box : { &platformBox, &sourceBox })
    {
        box->setColour (juce::ComboBox::backgroundColourId, panelColour);
        box->setColour (juce::ComboBox::outlineColourId,    lineColour);
        box->setColour (juce::ComboBox::textColourId,       textColour);
        box->setColour (juce::ComboBox::arrowColourId,      dimColour);
        addAndMakeVisible (*box);
    }

    for (auto* b : { &penaltyButton, &holdButton, &autoResetButton })
        b->setClickingTogglesState (true);

    for (auto* b : { &penaltyButton, &holdButton, &resetButton, &autoResetButton })
    {
        b->setColour (juce::TextButton::buttonColourId,   panelColour);
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3a6df0));
        b->setColour (juce::TextButton::textColourOffId,  dimColour);
        b->setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        addAndMakeVisible (*b);
    }

    resetButton.onClick = [this]
    {
        proc.requestReset();
        history.fill (-100.0f);
        historyPos = 0;
    };

    platformAtt  = std::make_unique<ComboAttachment>  (proc.apvts, "platform",  platformBox);
    sourceAtt    = std::make_unique<ComboAttachment>  (proc.apvts, "source",    sourceBox);
    penaltyAtt   = std::make_unique<ButtonAttachment> (proc.apvts, "penalty",   penaltyButton);
    holdAtt      = std::make_unique<ButtonAttachment> (proc.apvts, "hold",      holdButton);
    autoResetAtt = std::make_unique<ButtonAttachment> (proc.apvts, "autoreset", autoResetButton);

    setSize (760, 480);
    startTimerHz (30);
}

ScribaPenaltyAudioProcessorEditor::~ScribaPenaltyAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void ScribaPenaltyAudioProcessorEditor::timerCallback()
{
    const auto& m = proc.getMeter();
    integrated = m.getIntegrated();
    shortTerm  = m.getShortTerm();
    momentary  = m.getMomentary();
    truePeak   = m.getTruePeakDb();
    seconds    = m.getMeasuredSeconds();
    penalty    = proc.getPenaltyDb();
    limiterGr  = proc.getLimiterGrDb();

    if (++tick >= 3)
    {
        tick = 0;
        if (proc.isMeasuring() && seconds > 0.0f)
        {
            history[(size_t) historyPos] = shortTerm;
            historyPos = (historyPos + 1) % historySize;
        }
    }

    const auto& platform = scriba::platforms[(size_t) proc.getPlatformIndex()];
    const auto accent    = juce::Colour (platform.colour);

    penaltyButton.setColour (juce::TextButton::buttonOnColourId, accent.withMultipliedBrightness (0.55f));
    penaltyButton.setButtonText (penaltyButton.getToggleState()
                                    ? "PENALTY ON  -  ascolti come su " + juce::String (platform.name)
                                    : "BYPASS  -  master originale");

    repaint();
}

//==============================================================================
void ScribaPenaltyAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto& platform = scriba::platforms[(size_t) proc.getPlatformIndex()];
    const auto accent    = juce::Colour (platform.colour);

    g.fillAll (bgColour);

    // Intestazione
    auto header = getLocalBounds().removeFromTop (50);
    g.setColour (panelColour);
    g.fillRect (header);
    g.setColour (accent);
    g.fillRect (header.removeFromBottom (2));

    g.setColour (textColour);
    g.setFont (makeFont (24.0f, true));
    g.drawText ("SCRIBA PENALTY", 20, 0, 400, 48, juce::Justification::centredLeft);

    g.setColour (dimColour);
    g.setFont (makeFont (13.0f));
    g.drawText ("loudness penalty preview", 400, 0, 340, 48, juce::Justification::centredRight);

    drawPenaltyPanel (g, platform, accent);
    drawReadouts (g, platform);
    drawGraph (g, platform, accent);
}

void ScribaPenaltyAudioProcessorEditor::drawPenaltyPanel (juce::Graphics& g, const scriba::Platform& platform, juce::Colour accent)
{
    auto r = penaltyArea;
    g.setColour (panelColour);
    g.fillRoundedRectangle (r.toFloat(), 8.0f);

    r.reduce (16, 12);

    g.setColour (dimColour);
    g.setFont (makeFont (13.0f, true));
    g.drawText ("PENALTY " + juce::String (platform.name).toUpperCase(), r.removeFromTop (20), juce::Justification::centredLeft);

    const bool  useShort = proc.apvts.getRawParameterValue ("source")->load() > 0.5f;
    const bool  measured = (useShort ? shortTerm : integrated) > -70.0f;
    const auto  valueCol = std::abs (penalty) < 0.05f ? textColour : (penalty < 0.0f ? downColour : upColour);

    g.setColour (measured ? valueCol : dimColour);
    g.setFont (makeFont (64.0f, true));
    g.drawText (measured ? formatSigned (penalty) + " dB" : "-- dB", r.removeFromTop (78), juce::Justification::centredLeft);

    juce::String line1;

    if (! measured)
        line1 = "Fai suonare il brano dall'inizio alla fine";
    else if (penalty < -0.05f)
        line1 = "Il master viene abbassato di " + juce::String (-penalty, 1) + " dB";
    else if (penalty > 0.05f)
        line1 = "Il master viene alzato di " + juce::String (penalty, 1) + " dB";
    else
        line1 = "Nessuna correzione";

    g.setColour (textColour);
    g.setFont (makeFont (15.0f));
    g.drawText (line1, r.removeFromTop (24), juce::Justification::centredLeft);

    g.setColour (dimColour);
    g.setFont (makeFont (13.0f));
    g.drawText ("Target " + juce::String ((int) platform.targetLufs) + " LUFS  |  " + platform.rule,
                r.removeFromTop (22), juce::Justification::centredLeft);

    if (! penaltyButton.getToggleState())
    {
        g.setColour (accent);
        g.drawText ("BYPASS: stai ascoltando il master senza penalty", r.removeFromTop (22), juce::Justification::centredLeft);
    }
}

void ScribaPenaltyAudioProcessorEditor::drawReadouts (juce::Graphics& g, const scriba::Platform& platform)
{
    g.setColour (panelColour);
    g.fillRoundedRectangle (readoutArea.toFloat(), 8.0f);

    struct Item { juce::String label, value, unit; };

    const Item items[6] =
    {
        { "INTEGRATO",      formatLufs (integrated), "LUFS" },
        { "SHORT-TERM",     formatLufs (shortTerm),  "LUFS" },
        { "MOMENTARY",      formatLufs (momentary),  "LUFS" },
        { "TRUE PEAK MAX",  formatLufs (truePeak),   "dBTP" },
        { "TEMPO MISURATO", formatTime (seconds),    "" },
        { "LIMITER GR",     platform.limiterOnUp ? juce::String (limiterGr, 1) : juce::String ("--"), platform.limiterOnUp ? "dB" : "" },
    };

    auto area = readoutArea.reduced (14, 10);
    const int cellW = area.getWidth() / 2;
    const int cellH = area.getHeight() / 3;

    for (int i = 0; i < 6; ++i)
    {
        juce::Rectangle<int> cell (area.getX() + (i % 2) * cellW, area.getY() + (i / 2) * cellH, cellW, cellH);
        cell.reduce (6, 4);

        g.setColour (dimColour);
        g.setFont (makeFont (12.0f, true));
        g.drawText (items[i].label, cell.removeFromTop (18), juce::Justification::centredLeft);

        auto valueRow = cell.removeFromTop (40);

        g.setColour (textColour);
        g.setFont (makeFont (30.0f, true));
        g.drawText (items[i].value, valueRow.removeFromLeft (110), juce::Justification::centredLeft);

        g.setColour (dimColour);
        g.setFont (makeFont (13.0f));
        g.drawText (items[i].unit, valueRow, juce::Justification::centredLeft);
    }

    // Truepeak oltre -1 dBTP: avviso
    if (truePeak > scriba::peakCeilingDb)
    {
        g.setColour (downColour);
        g.setFont (makeFont (11.0f));
        g.drawText ("oltre -1 dBTP", readoutArea.reduced (14, 6).removeFromBottom (14), juce::Justification::centredRight);
    }
}

void ScribaPenaltyAudioProcessorEditor::drawGraph (juce::Graphics& g, const scriba::Platform& platform, juce::Colour accent)
{
    g.setColour (panelColour);
    g.fillRoundedRectangle (graphArea.toFloat(), 8.0f);

    auto plot = graphArea.reduced (40, 10).toFloat();

    const float minL = -36.0f, maxL = 0.0f;
    auto yFor = [&] (float lufs)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (lufs - minL) / (maxL - minL));
        return plot.getBottom() - t * plot.getHeight();
    };

    // Griglia
    g.setFont (makeFont (10.0f));
    for (float l = minL; l <= maxL; l += 6.0f)
    {
        const float y = yFor (l);
        g.setColour (lineColour);
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (dimColour);
        g.drawText (juce::String ((int) l), (int) graphArea.getX() + 6, (int) y - 7, 28, 14, juce::Justification::centredRight);
    }

    // Target della piattaforma
    {
        const float y = yFor (platform.targetLufs);
        const float dashes[] = { 6.0f, 4.0f };
        g.setColour (accent);
        g.drawDashedLine (juce::Line<float> (plot.getX(), y, plot.getRight(), y), dashes, 2, 1.5f);
    }

    // Integrato
    if (integrated > -70.0f)
    {
        g.setColour (textColour.withAlpha (0.6f));
        g.drawHorizontalLine ((int) yFor (integrated), plot.getX(), plot.getRight());
    }

    // Storico short-term
    juce::Path path;
    bool started = false;

    for (int j = 0; j < historySize; ++j)
    {
        const float v = history[(size_t) ((historyPos + j) % historySize)];
        const float x = plot.getX() + plot.getWidth() * (float) j / (float) (historySize - 1);

        if (v <= -70.0f) { started = false; continue; }

        if (! started) { path.startNewSubPath (x, yFor (v)); started = true; }
        else           { path.lineTo (x, yFor (v)); }
    }

    g.setColour (juce::Colour (0xff6fa8ff));
    g.strokePath (path, juce::PathStrokeType (1.8f));

    g.setColour (dimColour);
    g.setFont (makeFont (10.0f));
    g.drawText ("short-term (30 s)   - - target   ---- integrato",
                graphArea.reduced (12, 4).removeFromTop (14), juce::Justification::topRight);
}

//==============================================================================
void ScribaPenaltyAudioProcessorEditor::resized()
{
    platformBox    .setBounds (20,  62, 230, 32);
    sourceBox      .setBounds (260, 62, 200, 32);
    autoResetButton.setBounds (470, 62, 110, 32);
    holdButton     .setBounds (590, 62, 70,  32);
    resetButton    .setBounds (670, 62, 70,  32);

    penaltyArea = { 20,  106, 360, 196 };
    penaltyButton.setBounds (20, 310, 360, 42);
    readoutArea = { 390, 106, 350, 246 };
    graphArea   = { 20,  362, 720, 104 };
}
