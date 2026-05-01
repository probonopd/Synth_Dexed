#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <functional>
#include <vector>
#include "TrackChannelDialog.h"

class MidiBrowserComponent : public juce::Component,
                              public juce::ListBoxModel {
public:
    MidiBrowserComponent();
    ~MidiBrowserComponent() override;

    void resized() override;
    void paint(juce::Graphics&) override;

    // Fired on message thread when a processed MIDI file is ready
    std::function<void(juce::MidiFile, juce::String)> onMidiReady;

    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    void selectedRowsChanged(int lastRow) override;

private:
    struct BrowseEntry {
        juce::String path;
        bool isDirectory { false };
    };

    juce::TextEditor searchBox;
    juce::Label breadcrumbLabel;
    juce::ListBox listBox;
    juce::TextButton upButton { "Up" };
    juce::TextButton loadButton { "Load & Play" };
    juce::Label statusLabel;
    juce::ToggleButton filterBankButton { "Filter bank/voice changes" };

    std::vector<BrowseEntry> entries;
    juce::StringArray dirStack;
    juce::String currentPath { "/" };

    class DownloadThread;
    std::unique_ptr<DownloadThread> downloadThread;

    std::unique_ptr<juce::DialogWindow> trackDialogWindow;
    std::unique_ptr<TrackChannelDialog> trackDialogComp;

    void loadDirectory(const juce::String& path);
    void doSearch(const juce::String& query);
    void downloadAndProcess(const BrowseEntry& entry);
    void showTrackDialog(const juce::File& cachedFile, const juce::String& fileName);
    void processAndFire(const juce::MidiFile& midi, const juce::String& fileName,
                        const std::vector<TrackAssignment>& assignments);

    juce::File getCacheDir() const;
    juce::File getCachedFile(const juce::String& url) const;
    void setStatus(const juce::String& msg, bool error = false);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiBrowserComponent)
};
