/*
 * Copyright (c) 2020 Andreas Pohl
 * Licensed under MIT (https://github.com/apohl79/audiogridder/blob/master/COPYING)
 *
 * Author: Andreas Pohl
 */

#include "PluginMonitor.hpp"
#include "PluginProcessor.hpp"
#include "Images.hpp"
#include "WindowPositions.hpp"
#include "Metrics.hpp"

namespace e47 {

PluginStatus::PluginStatus(AudioGridderAudioProcessor* plugin) {
    ok = plugin->getClient().isReadyLockFree();
    auto track = plugin->getTrackProperties();
    channelName = track.name;
    channelColour = track.colour;
    loadedPlugins = plugin->getLoadedPluginsString();
    String statId = "audio.";
    statId << plugin->getId();
    auto ts = Metrics::getStatistic<TimeStatistic>(statId);
    perf95th = ts->get1minHistogram().nintyFifth;
}

PluginMonitorWindow::PluginMonitorWindow(PluginMonitor* mon, const String& mode)
    : TopLevelWindow("AudioGridder - " + mode, true), LogTagDelegate(mon), m_mon(mon) {
    traceScope();

    auto& lf = getLookAndFeel();
    lf.setColour(ResizableWindow::backgroundColourId, Colour(Defaults::BG_COLOR));

    m_logo.setImage(ImageCache::getFromMemory(Images::logo_png, Images::logo_pngSize));
    m_logo.setBounds(10, 10, 16, 16);
    m_logo.setAlpha(0.3f);
    m_logo.addMouseListener(this, true);
    addAndMakeVisible(m_logo);

    m_title.setText("Plugin Monitor - " + mode, NotificationType::dontSendNotification);
    m_title.setBounds(30, 10, m_totalWidth - 30, 16);
    auto f = m_title.getFont();
    f.setHeight(f.getHeight() - 2);
    f.setBold(true);
    m_title.setFont(f);
    m_title.setAlpha(0.8f);
    m_title.addMouseListener(this, true);
    addAndMakeVisible(m_title);

    updatePosition();
    setAlwaysOnTop(true);
    setVisible(true);
}

PluginMonitorWindow::~PluginMonitorWindow() {
    traceScope();
    WindowPositions::PositionType pt = WindowPositions::PluginMonFx;
#if JucePlugin_IsSynth
    pt = WindowPositions::PluginMonInst;
#elif JucePlugin_IsMidiEffect
    pt = WindowPositions::PluginMonMidi;
#endif
    WindowPositions::set(pt, {});
}

void PluginMonitorWindow::mouseUp(const MouseEvent& /*event*/) {
    setVisible(false);
    PluginMonitor::setAlwaysShow(false);
    m_mon->hideWindow();
}

void PluginMonitorWindow::update(const Array<PluginStatus>& status) {
    for (auto& comp : m_components) {
        removeChildComponent(comp.get());
    }
    m_components.clear();

    int borderLR = 15;  // left/right border
    int borderTB = 15;  // top/bottom border
    int rowHeight = 18;

    int colWidth[] = {20, 100, 190, 65, 10};

    auto getLabelBounds = [&](int r, int c, int span = 1) {
        int left = borderLR;
        for (int i = 0; i < c; i++) {
            left += colWidth[i];
        }
        int width = 0;
        for (int i = c; i < c + span; i++) {
            width += colWidth[i];
        }
        return Rectangle<int>(left, borderTB + r * rowHeight, width, rowHeight);
    };

    auto getLineBounds = [&](int r) {
        return Rectangle<int>(borderLR + 2, borderTB + r * rowHeight - 1, m_totalWidth - borderLR * 2, 1);
    };

    int row = 1;

    addLabel("Channel", getLabelBounds(row, 0, 2), Justification::topLeft, 1.0f);
    addLabel("Loaded Chain", getLabelBounds(row, 2), Justification::topLeft, 1.0f);
    addLabel("Perf", getLabelBounds(row, 3), Justification::topRight, 1.0f);

    row++;

    for (auto& s : status) {
        auto line = std::make_unique<HirozontalLine>(getLineBounds(row));
        addChildAndSetID(line.get(), "line");
        m_components.push_back(std::move(line));

        auto chan = std::make_unique<Channel>(getLabelBounds(row, 0), s.channelColour);
        addChildAndSetID(chan.get(), "led");
        m_components.push_back(std::move(chan));
        addLabel(s.channelName, getLabelBounds(row, 1));
        addLabel(s.loadedPlugins, getLabelBounds(row, 2));
        addLabel(String(s.perf95th, 2) + " ms", getLabelBounds(row, 3), Justification::topRight);
        auto led = std::make_unique<Status>(getLabelBounds(row, 4), s.ok);
        addChildAndSetID(led.get(), "led");
        m_components.push_back(std::move(led));

        row++;
    }

    for (auto* c : getChildren()) {
        c->addMouseListener(this, true);
    }

    m_totalHeight = rowHeight * row + borderTB + 5;
    updatePosition();
}

void PluginMonitorWindow::addLabel(const String& txt, Rectangle<int> bounds, Justification just, float alpha) {
    auto label = std::make_unique<Label>();
    label->setText(txt, NotificationType::dontSendNotification);
    auto f = label->getFont();
    f.setHeight(f.getHeight() - 2);
    label->setFont(f);
    label->setAlpha(alpha);
    label->setBounds(bounds);
    label->setJustificationType(just);
    addChildAndSetID(label.get(), "lbl");
    m_components.push_back(std::move(label));
}

void PluginMonitorWindow::updatePosition() {
    auto desktopRect = Desktop::getInstance().getDisplays().getMainDisplay().totalArea;
    int x = desktopRect.getWidth() - m_totalWidth - 20;
    int y = 50;
    WindowPositions::PositionType pt = WindowPositions::PluginMonFx;
    Rectangle<int> upperBounds;

#if JucePlugin_IsSynth
    pt = WindowPositions::PluginMonInst;
    upperBounds = WindowPositions::get(WindowPositions::PluginMonFx, {});
#elif JucePlugin_IsMidiEffect
    pt = WindowPositions::PluginMonMidi;
    upperBounds = WindowPositions::get(WindowPositions::PluginMonInst, {});
    if (upperBounds.isEmpty()) {
        upperBounds = WindowPositions::get(WindowPositions::PluginMonFx, {});
    }
#endif

    if (!upperBounds.isEmpty()) {
        y = upperBounds.getBottom() + 20;
    }

    setBounds(x, y, m_totalWidth, m_totalHeight);
    WindowPositions::set(pt, getBounds());
}

void PluginMonitorWindow::Channel::paint(Graphics& g) {
    int len = 12;
    int x = 4;
    int y = 2;
    g.setColour(m_col);
    g.fillRoundedRectangle(x, y, len, len, 3);
    g.setColour(Colours::white);
    g.setOpacity(0.1f);
    g.drawRoundedRectangle(x, y, len, len, 3, 1);
}

void PluginMonitorWindow::Status::paint(Graphics& g) {
    int rad = 3;
    int x = getWidth() / 2 - rad;
    int y = getHeight() / 2 - rad;
    Path p;
    p.addEllipse(x, y, rad * 2, rad * 2);
    g.setColour(m_col);
    g.setOpacity(0.9f);
    g.fillPath(p);
}

void PluginMonitorWindow::HirozontalLine::paint(Graphics& g) {
    g.setColour(Colours::white);
    g.setOpacity(0.05f);
    g.fillAll();
}

std::mutex PluginMonitor::m_pluginMtx;
Array<AudioGridderAudioProcessor*> PluginMonitor::m_plugins;

void PluginMonitor::run() {
    traceScope();

    logln("plugin monitor started");

    String mode;
#if JucePlugin_IsSynth
    mode = "Instruments";
#elif JucePlugin_IsMidiEffect
    mode = "Midi";
#else
    mode = "FX";
#endif

    while (!currentThreadShouldExit()) {
        if (m_windowAlwaysShow || m_windowAutoShow || m_windowActive) {
            bool allOk = true;
            Array<PluginStatus> status;
            {
                std::lock_guard<std::mutex> lock(m_pluginMtx);
                for (auto plugin : m_plugins) {
                    PluginStatus s(plugin);
                    allOk = allOk && s.ok;
                    status.add(std::move(s));
                }
            }

            bool show = !m_windowWantsHide && ((!allOk && m_windowAutoShow) || m_windowAlwaysShow);
            bool hide = m_windowWantsHide || (!m_windowAlwaysShow && (allOk || !m_windowAutoShow));
            if (show) {
                m_windowActive = true;
            } else if (hide) {
                m_windowActive = false;
            }
            m_windowWantsHide = false;

            runOnMsgThreadAsync([this, mode, status, show, hide] {
                traceScope();
                if (show && nullptr == m_window) {
                    logln("showing monitor window");
                    m_window = std::make_unique<PluginMonitorWindow>(this, mode);
                } else if (nullptr != m_window && hide) {
                    logln("hiding monitor window");
                    m_window.reset();
                }
                if (nullptr != m_window) {
                    m_window->update(status);
                }
            });
        }
        int sleepTime = m_windowActive ? 500 : 2000;
        sleepExitAwareWithCondition(sleepTime, [this]() -> bool { return !m_windowActive && m_windowAlwaysShow; });
    }

    logln("plugin monitor terminated");
}

void PluginMonitor::add(AudioGridderAudioProcessor* plugin) {
    std::lock_guard<std::mutex> lock(m_pluginMtx);
    m_plugins.addIfNotAlreadyThere(plugin);
}

void PluginMonitor::remove(AudioGridderAudioProcessor* plugin) {
    std::lock_guard<std::mutex> lock(m_pluginMtx);
    m_plugins.removeAllInstancesOf(plugin);
}

}  // namespace e47