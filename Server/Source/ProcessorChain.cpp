/*
 * Copyright (c) 2020 Andreas Pohl
 * Licensed under MIT (https://github.com/apohl79/audiogridder/blob/master/COPYING)
 *
 * Author: Andreas Pohl
 */

#include "ProcessorChain.hpp"
#include "NumberConversion.hpp"
#include "App.hpp"

namespace e47 {

std::atomic_uint32_t AGProcessor::count{0};
std::atomic_uint32_t AGProcessor::loadedCount{0};

AGProcessor::AGProcessor(ProcessorChain& chain, const String& id, double sampleRate, int blockSize)
    : LogTagDelegate(chain.getLogTagSource()),
      m_chain(chain),
      m_id(id),
      m_sampleRate(sampleRate),
      m_blockSize(blockSize) {
    count++;
}

AGProcessor::~AGProcessor() {
    unload();
    count--;
}

String AGProcessor::createPluginID(const PluginDescription& d, bool useJuce) {
    if (useJuce) {
        return d.createIdentifierString();
    } else {
        return d.pluginFormatName + "-" + d.name + "-" + String::toHexString(d.uid);
    }
}

String AGProcessor::createPluginID(const PluginDescription& d) {
    return createPluginID(d, getApp()->getServer().getUseJucePluginIDs());
}

std::unique_ptr<PluginDescription> AGProcessor::findPluginDescritpion(const String& id) {
    auto& pluglist = getApp()->getPluginList();
    std::unique_ptr<PluginDescription> plugdesc;
    for (auto& desc : pluglist.getTypes()) {
        if (createPluginID(desc) == id) {
            plugdesc = std::make_unique<PluginDescription>(desc);
        }
    }
    // fallback with juce ID
    if (nullptr == plugdesc) {
        plugdesc = pluglist.getTypeForIdentifierString(id);
    }
    // fallback with filename
    if (nullptr == plugdesc) {
        plugdesc = pluglist.getTypeForFile(id);
    }
    return plugdesc;
}

std::shared_ptr<AudioPluginInstance> AGProcessor::loadPlugin(PluginDescription& plugdesc, double sampleRate,
                                                             int blockSize, String& err) {
    setLogTagStatic("agprocessor");
    traceScope();
    String err2;
    AudioPluginFormatManager plugmgr;
    plugmgr.addDefaultFormats();
    // std::lock_guard<std::mutex> lock(m_pluginLoaderMtx);  // don't load plugins in parallel
    std::shared_ptr<AudioPluginInstance> inst;
    runOnMsgThreadSync([&] {
        traceScope();
        inst =
            std::shared_ptr<AudioPluginInstance>(plugmgr.createPluginInstance(plugdesc, sampleRate, blockSize, err2));
    });
    if (nullptr == inst) {
        err = "failed loading plugin ";
        err << plugdesc.fileOrIdentifier << ": " << err2;
        logln(err);
    }
    return inst;
}

std::shared_ptr<AudioPluginInstance> AGProcessor::loadPlugin(const String& id, double sampleRate, int blockSize,
                                                             String& err) {
    setLogTagStatic("agprocessor");
    traceScope();
    auto plugdesc = findPluginDescritpion(id);
    if (nullptr != plugdesc) {
        return loadPlugin(*plugdesc, sampleRate, blockSize, err);
    } else {
        err = "failed to find plugin descriptor";
        logln(err);
    }
    return nullptr;
}

bool AGProcessor::load(String& err) {
    traceScope();
    bool loaded = false;
    std::shared_ptr<AudioPluginInstance> p;
    {
        std::lock_guard<std::mutex> lock(m_pluginMtx);
        p = m_plugin;
    }
    if (nullptr == p) {
        p = loadPlugin(m_id, m_sampleRate, m_blockSize, err);
        if (nullptr != p) {
            if (m_chain.initPluginInstance(p, err)) {
                loaded = true;
                std::lock_guard<std::mutex> lock(m_pluginMtx);
                m_plugin = p;
                loadedCount++;
            }
        }
    }
    return loaded;
}

void AGProcessor::unload() {
    traceScope();
    std::shared_ptr<AudioPluginInstance> p;
    {
        std::lock_guard<std::mutex> lock(m_pluginMtx);
        if (nullptr != m_plugin) {
            if (prepared) {
                m_plugin->releaseResources();
            }
            p = m_plugin;
            m_plugin.reset();
            loadedCount--;
        }
    }
}

void AGProcessor::suspendProcessing(const bool shouldBeSuspended) {
    traceScope();
    auto p = getPlugin();
    if (nullptr != p) {
        if (shouldBeSuspended) {
            p->suspendProcessing(true);
            p->releaseResources();
        } else {
            p->prepareToPlay(m_chain.getSampleRate(), m_chain.getBlockSize());
            p->suspendProcessing(false);
        }
    }
}

void ProcessorChain::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) {
    traceScope();
    setRateAndBufferSizeDetails(sampleRate, maximumExpectedSamplesPerBlock);
    std::lock_guard<std::mutex> lock(m_processors_mtx);
    for (auto& proc : m_processors) {
        proc->prepareToPlay(sampleRate, maximumExpectedSamplesPerBlock);
    }
}

void ProcessorChain::releaseResources() {
    traceScope();
    std::lock_guard<std::mutex> lock(m_processors_mtx);
    for (auto& proc : m_processors) {
        proc->releaseResources();
    }
}

void ProcessorChain::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages) {
    traceScope();
    auto start_proc = Time::getHighResolutionTicks();
    processBlockReal(buffer, midiMessages);
    auto end_proc = Time::getHighResolutionTicks();
    double time_proc = Time::highResolutionTicksToSeconds(end_proc - start_proc);
    if (time_proc > 0.02) {
        logln("warning: chain (" << toString() << "): high audio processing time: " << time_proc);
    }
}

void ProcessorChain::processBlock(AudioBuffer<double>& buffer, MidiBuffer& midiMessages) {
    traceScope();
    auto start_proc = Time::getHighResolutionTicks();
    processBlockReal(buffer, midiMessages);
    auto end_proc = Time::getHighResolutionTicks();
    double time_proc = Time::highResolutionTicksToSeconds(end_proc - start_proc);
    if (time_proc > 0.02) {
        logln("warning: chain (" << toString() << "): high audio processing time: " << time_proc);
    }
}

double ProcessorChain::getTailLengthSeconds() const { return m_tailSecs; }

bool ProcessorChain::supportsDoublePrecisionProcessing() const { return m_supportsDoublePrecission; }

bool ProcessorChain::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (layouts.getMainOutputChannelSet() != AudioChannelSet::mono() &&
        layouts.getMainOutputChannelSet() != AudioChannelSet::stereo() &&
        layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet()) {
        return false;
    }
    return true;
}

bool ProcessorChain::updateChannels(int channelsIn, int channelsOut) {
    traceScope();
    AudioProcessor::BusesLayout layout;
    if (channelsIn == 1) {
        layout.inputBuses.add(AudioChannelSet::mono());
    } else if (channelsIn == 2) {
        layout.inputBuses.add(AudioChannelSet::stereo());
    }
    if (channelsOut == 1) {
        layout.outputBuses.add(AudioChannelSet::mono());
    } else if (channelsOut == 2) {
        layout.outputBuses.add(AudioChannelSet::stereo());
    }
    setBusesLayout(layout);
    std::lock_guard<std::mutex> lock(m_processors_mtx);
    m_extraChannels = 0;
    for (auto& proc : m_processors) {
        auto p = proc->getPlugin();
        if (nullptr == p || !setProcessorBusesLayout(p)) {
            return false;
        }
    }
    return true;
}

bool ProcessorChain::setProcessorBusesLayout(std::shared_ptr<AudioPluginInstance> proc) {
    traceScope();
    auto layout = getBusesLayout();
    if (proc->checkBusesLayoutSupported(layout)) {
        return proc->setBusesLayout(layout);
    } else {
        // try to figure out if we can add some extra channels to make the plugin work
        auto procLayout = proc->getBusesLayout();
        // main bus IN
        int extraInChannels = procLayout.getMainInputChannels() - layout.getMainInputChannels();
        // check extra busses IN
        for (int busIdx = 1; busIdx < procLayout.inputBuses.size(); busIdx++) {
            auto bus = procLayout.inputBuses[busIdx];
            extraInChannels += bus.size();
            layout.inputBuses.add(bus);
        }
        // main bus OUT
        int extraOutChannels = procLayout.getMainOutputChannels() - layout.getMainOutputChannels();
        // check extra busses OUT
        for (int busIdx = 1; busIdx < procLayout.outputBuses.size(); busIdx++) {
            auto bus = procLayout.outputBuses[busIdx];
            extraOutChannels += bus.size();
            layout.outputBuses.add(bus);
        }

        if ((extraInChannels > 0 || extraOutChannels > 0) && proc->checkBusesLayoutSupported(layout) &&
            proc->setBusesLayout(layout)) {
            m_extraChannels = jmax(m_extraChannels, extraInChannels, extraOutChannels);
            logln(extraInChannels << " extra input(s), " << extraOutChannels << " extra output(s)");
            return true;
        }
    }
    return false;
}

int ProcessorChain::getExtraChannels() {
    traceScope();
    std::lock_guard<std::mutex> lock(m_processors_mtx);
    return m_extraChannels;
}

bool ProcessorChain::initPluginInstance(std::shared_ptr<AudioPluginInstance> inst, String& err) {
    traceScope();
    if (!setProcessorBusesLayout(inst)) {
        err = "I/O layout (" + String(getMainBusNumInputChannels()) + "," + String(getMainBusNumOutputChannels()) +
              " +" + String(m_extraChannels) + ") not supported by plugin: " + inst->getName();
        logln(err);
        return false;
    }
    AudioProcessor::ProcessingPrecision prec = AudioProcessor::singlePrecision;
    if (isUsingDoublePrecision() && supportsDoublePrecisionProcessing()) {
        if (inst->supportsDoublePrecisionProcessing()) {
            prec = AudioProcessor::doublePrecision;
        } else {
            logln("host wants double precission but plugin '" << inst->getName() << "' does not support it");
        }
    }
    inst->setProcessingPrecision(prec);
    inst->prepareToPlay(getSampleRate(), getBlockSize());
    inst->setPlayHead(getPlayHead());
    if (prec == AudioProcessor::doublePrecision) {
        preProcessBlocks<double>(inst);
    } else {
        preProcessBlocks<float>(inst);
    }
    return true;
}

bool ProcessorChain::addPluginProcessor(const String& id, String& err) {
    traceScope();
    auto proc = std::make_shared<AGProcessor>(*this, id, getSampleRate(), getBlockSize());
    if (proc->load(err)) {
        addProcessor(proc);
        return true;
    }
    return false;
}

void ProcessorChain::addProcessor(std::shared_ptr<AGProcessor> processor) {
    traceScope();
    std::lock_guard<std::mutex> lock(m_processors_mtx);
    m_processors.push_back(processor);
    updateNoLock();
}

void ProcessorChain::delProcessor(int idx) {
    traceScope();
    int i = 0;
    std::lock_guard<std::mutex> lock(m_processors_mtx);
    for (auto it = m_processors.begin(); it < m_processors.end(); it++) {
        if (i++ == idx) {
            m_processors.erase(it);
            break;
        }
    }
    updateNoLock();
}

void ProcessorChain::update() {
    traceScope();
    std::lock_guard<std::mutex> lock(m_processors_mtx);
    updateNoLock();
}

void ProcessorChain::updateNoLock() {
    traceScope();
    int latency = 0;
    bool supportsDouble = true;
    m_extraChannels = 0;
    for (auto& proc : m_processors) {
        auto p = proc->getPlugin();
        if (nullptr != p && !p->isSuspended()) {
            latency += p->getLatencySamples();
            if (!p->supportsDoublePrecisionProcessing()) {
                supportsDouble = false;
            }
            int extraInChannels = p->getTotalNumInputChannels() - p->getMainBusNumInputChannels();
            int extraOutChannels = p->getTotalNumOutputChannels() - p->getMainBusNumOutputChannels();
            m_extraChannels = jmax(m_extraChannels, extraInChannels, extraOutChannels);
        }
    }
    if (latency != getLatencySamples()) {
        logln("updating latency samples to " << latency);
        setLatencySamples(latency);
    }
    m_supportsDoublePrecission = supportsDouble;
    auto it = m_processors.rbegin();
    while (it != m_processors.rend() && (*it)->isSuspended()) {
        it++;
    }
    if (it != m_processors.rend()) {
        m_tailSecs = (*it)->getTailLengthSeconds();
    } else {
        m_tailSecs = 0.0;
    }
}

std::shared_ptr<AGProcessor> ProcessorChain::getProcessor(int index) {
    traceScope();
    std::lock_guard<std::mutex> lock(m_processors_mtx);
    if (index > -1 && as<size_t>(index) < m_processors.size()) {
        return m_processors[as<size_t>(index)];
    }
    return nullptr;
}

void ProcessorChain::exchangeProcessors(int idxA, int idxB) {
    traceScope();
    std::lock_guard<std::mutex> lock(m_processors_mtx);
    if (idxA > -1 && as<size_t>(idxA) < m_processors.size() && idxB > -1 && as<size_t>(idxB) < m_processors.size()) {
        std::swap(m_processors[as<size_t>(idxA)], m_processors[as<size_t>(idxB)]);
    }
}

float ProcessorChain::getParameterValue(int idx, int paramIdx) {
    traceScope();
    std::lock_guard<std::mutex> lock(m_processors_mtx);
    if (idx > -1 && as<size_t>(idx) < m_processors.size()) {
        auto p = m_processors[as<size_t>(idx)]->getPlugin();
        if (nullptr != p) {
            for (auto& param : p->getParameters()) {
                if (paramIdx == param->getParameterIndex()) {
                    return param->getValue();
                }
            }
        }
    }
    return 0;
}

void ProcessorChain::clear() {
    traceScope();
    releaseResources();
    std::lock_guard<std::mutex> lock(m_processors_mtx);
    m_processors.clear();
}

String ProcessorChain::toString() {
    traceScope();
    String ret;
    std::lock_guard<std::mutex> lock(m_processors_mtx);
    bool first = true;
    for (auto& proc : m_processors) {
        if (!first) {
            ret << " > ";
        } else {
            first = false;
        }
        if (proc->isSuspended()) {
            ret << "<bypassed>";
        } else {
            ret << proc->getName();
        }
    }
    return ret;
}

}  // namespace e47
