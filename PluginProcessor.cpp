#include "PluginProcessor.h"
#include "PluginEditor.h"

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties()
          .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    dspEngine.prepare(sampleRate, samplesPerBlock,
                      getTotalNumOutputChannels());
}

void PluginProcessor::releaseResources() {
    dspEngine.releaseResources();
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto mono   = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();
    const auto out    = layouts.getMainOutputChannelSet();
    if (out != mono && out != stereo) return false;
    return layouts.getMainInputChannelSet() == out;
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                   juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;

    // Snapshot host transport for tempo-sync / transport-triggered modulators
    TransportInfo transport;
    if (auto* playHead = getPlayHead()) {
        if (auto pos = playHead->getPosition()) {
            if (auto bpm = pos->getBpm())         transport.bpm         = *bpm;
            if (auto ppq = pos->getPpqPosition()) transport.ppqPosition = *ppq;
            transport.isPlaying = pos->getIsPlaying();
        }
    }
    transport.justStarted = transport.isPlaying && !wasPlaying;
    wasPlaying = transport.isPlaying;

    // Collect note events (sample-accurate envelope / XLFO triggering)
    ModTriggerList triggers;
    for (const auto metadata : midi) {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn())
            triggers.add({ metadata.samplePosition, true, msg.getFloatVelocity() });
        else if (msg.isNoteOff())
            triggers.add({ metadata.samplePosition, false, 0.0f });
    }

    dspEngine.process(buffer, buffer.getNumSamples(), transport, triggers);
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() {
    return new PluginEditor(*this);
}

// ── State persistence ────────────────────────────────────────────────────────

void PluginProcessor::getStateInformation(juce::MemoryBlock& dest) {
    juce::XmlElement root("MultiVitaminState");

    root.setAttribute("dryWet",   (double)dspEngine.globalDryWet.load());
    root.setAttribute("cursorX", (double)dspEngine.cursorRelX.load());
    root.setAttribute("cursorY", (double)dspEngine.cursorRelY.load());

    for (int n = 0; n < DspEngine::NUM_NODES; ++n) {
        auto* node = root.createNewChildElement("Node");
        node->setAttribute("index",  n);
        node->setAttribute("type",   (int)dspEngine.getNodeType(n));
        node->setAttribute("param1", (double)dspEngine.getNodeParam(n, 0));
        node->setAttribute("param2", (double)dspEngine.getNodeParam(n, 1));
    }

    dspEngine.modMatrix.toXml(root);

    copyXmlToBinary(root, dest);
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml || xml->getTagName() != "MultiVitaminState") return;

    dspEngine.globalDryWet.store((float)xml->getDoubleAttribute("dryWet",   1.0));
    dspEngine.cursorRelX  .store((float)xml->getDoubleAttribute("cursorX", 0.5));
    dspEngine.cursorRelY  .store((float)xml->getDoubleAttribute("cursorY", 0.5));

    for (auto* node : xml->getChildWithTagNameIterator("Node")) {
        const int n = node->getIntAttribute("index");
        if (n < 0 || n >= DspEngine::NUM_NODES) continue;
        dspEngine.setNodeType(n, (FxNode::Type)node->getIntAttribute("type"));
        dspEngine.setNodeParam(n, 0, (float)node->getDoubleAttribute("param1", 0.5));
        dspEngine.setNodeParam(n, 1, (float)node->getDoubleAttribute("param2", 0.5));
    }

    dspEngine.modMatrix.fromXml(*xml);
}

// Required by JUCE to instantiate the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
