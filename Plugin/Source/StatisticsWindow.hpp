/*
 * Copyright (c) 2020 Andreas Pohl
 * Licensed under MIT (https://github.com/apohl79/audiogridder/blob/master/COPYING)
 *
 * Author: Andreas Pohl
 */

#ifndef StatisticsWindow_hpp
#define StatisticsWindow_hpp

#include <JuceHeader.h>

#include "Utils.hpp"

class AudioGridderAudioProcessorEditor;

namespace e47 {

class StatisticsWindow : public DocumentWindow, public LogTag {
  public:
    StatisticsWindow(AudioGridderAudioProcessorEditor* editor);
    ~StatisticsWindow() override;

    void closeButtonPressed() override;

    class HirozontalLine : public Component {
      public:
        HirozontalLine(Rectangle<int> bounds) { setBounds(bounds); }
        void paint(Graphics& g) override;
    };

  private:
    AudioGridderAudioProcessorEditor* m_editor;
    std::vector<std::unique_ptr<Component>> m_components;
    Label m_totalClients, m_audioRPS, m_audioPTavg, m_audioPTmin, m_audioPTmax, m_audioPT95th, m_audioBytesOut,
        m_audioBytesIn;
    int m_blockSize;
    int m_channels;
    bool m_doublePrecission;

    class Updater : public Thread, public LogTagDelegate {
      public:
        Updater(LogTag* tag) : Thread("StatsUpdater"), LogTagDelegate(tag) {
            traceScope();
            initAsyncFunctors();
        }

        ~Updater() override {
            traceScope();
            stopAsyncFunctors();
        }

        void set(std::function<void()> fn) { m_fn = fn; }

        void run() override {
            traceScope();
            while (!currentThreadShouldExit()) {
                runOnMsgThreadAsync([this] { m_fn(); });
                // Relax
                int sleepstep = 50;
                int sleepfor = 1000 / sleepstep;
                while (!currentThreadShouldExit() && sleepfor-- > 0) {
                    Thread::sleep(sleepstep);
                }
            }
        }

      private:
        std::function<void()> m_fn;

        ENABLE_ASYNC_FUNCTORS();
    };
    Updater m_updater;

    void addLabel(const String& txt, Rectangle<int> bounds);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatisticsWindow)
};

}  // namespace e47

#endif /* StatisticsWindow_hpp */
