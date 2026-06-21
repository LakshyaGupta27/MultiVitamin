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
                                   juce::MidiBuffer& /*midi*/) {
    juce::ScopedNoDenormals noDenormals;
    dspEngine.process(buffer, buffer.getNumSamples());
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

    for (int i = 0; i < DspEngine::MAX_LFOS; ++i) {
        auto& ls  = dspEngine.lfoStates[i];
        auto* lfo = root.createNewChildElement("Lfo");
        lfo->setAttribute("index",       i);
        lfo->setAttribute("active",      ls.active.load());
        lfo->setAttribute("shape",       ls.shape.load());
        lfo->setAttribute("rate",        (double)ls.rate.load());
        lfo->setAttribute("depth",       (double)ls.depth.load());
        lfo->setAttribute("targetType",  ls.targetType.load());
        lfo->setAttribute("targetNode",  ls.targetNode.load());
        lfo->setAttribute("targetParam", ls.targetParam.load());
    }

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

    for (auto* lfo : xml->getChildWithTagNameIterator("Lfo")) {
        const int i = lfo->getIntAttribute("index");
        if (i < 0 || i >= DspEngine::MAX_LFOS) continue;
        auto& ls = dspEngine.lfoStates[i];
        ls.active     .store(lfo->getBoolAttribute ("active"));
        ls.shape      .store(lfo->getIntAttribute  ("shape"));
        ls.rate       .store((float)lfo->getDoubleAttribute("rate",  0.5));
        ls.depth      .store((float)lfo->getDoubleAttribute("depth", 0.3));
        ls.targetType .store(lfo->getIntAttribute  ("targetType"));
        ls.targetNode .store(lfo->getIntAttribute  ("targetNode"));
        ls.targetParam.store(lfo->getIntAttribute  ("targetParam"));
    }
}

// Required by JUCE to instantiate the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
