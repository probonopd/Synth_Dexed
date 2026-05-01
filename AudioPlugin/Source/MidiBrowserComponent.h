#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>
#include "TrackChannelDialog.h"

class MidiBrowserComponent : public juce::Component,
                              public juce::ListBoxModel,
                              public juce::DragAndDropContainer
{
public:
    MidiBrowserComponent();
    ~MidiBrowserComponent() override;

    void resized() override;
    void paint(juce::Graphics&) override;

    std::function<void(juce::MidiFile, juce::String)> onMidiReady;
    juce::String getLastModifiedMidiPath() const { return lastModifiedPath; }

    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;

    void loadDirectory(const juce::String& path);
    void doSearch(const juce::String& query);

private:
    struct BrowseEntry {
        juce::String path;
        bool isDirectory;
    };

    void onItemActivated(int row);
    void showTrackChannelDialog(const juce::File& midiFile, const juce::String& fileName);
    juce::File getCacheDir() const;
    juce::File getCachedMidiFile(const juce::String& url) const;
    void downloadMidiAsync(const juce::String& url, const juce::String& fileName,
                           std::function<void(juce::File, juce::String)> callback);

    juce::TextEditor searchBox;
    juce::Label breadcrumbLabel;
    juce::ListBox listBox;
    juce::TextButton upButton { "Up" };
    juce::TextButton sendButton { "Load & Play" };
    juce::Label statusLabel;
    juce::ToggleButton replaceGMButton { "Replace GM with DX7 voices" };
    juce::ToggleButton filterBankButton { "Filter bank/voice changes" };

    std::vector<BrowseEntry> entries;
    juce::StringArray dirStack;
    juce::String currentPath;
    juce::String lastModifiedPath;

    std::unique_ptr<juce::DialogWindow> trackDialog;
    std::unique_ptr<TrackChannelDialog> trackChannelComp;

    struct DownloadJob : public juce::Thread {
        juce::String url, fileName;
        juce::File destFile;
        std::function<void(juce::File, juce::String)> callback;
        DownloadJob(juce::String u, juce::String n, juce::File d,
                    std::function<void(juce::File, juce::String)> cb)
            : juce::Thread("MidiDownload"), url(std::move(u)), fileName(std::move(n)),
              destFile(std::move(d)), callback(std::move(cb)) {}
        void run() override;
    };
    std::unique_ptr<DownloadJob> downloadJob;

    void setStatus(const juce::String& msg, bool error = false);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiBrowserComponent)
};
