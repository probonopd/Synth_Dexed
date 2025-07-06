#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "../../src/FMRack/VoiceData.h"

class VoiceBrowserComponent : public juce::Component, public juce::ListBoxModel, public juce::KeyListener {
public:
    VoiceBrowserComponent();
    ~VoiceBrowserComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    void selectedRowsChanged(int lastRowSelected) override;

    void setStatus(const juce::String& msg, bool error = false);
    void parseVoiceList(const juce::String& json);
    void filterVoices();
    void loadVoiceList();
    void downloadVoiceSyx(int index);    void sendVoiceSyx(const juce::MemoryBlock& syx, int midiChannel);
    void downloadBankInfo(int index);
    juce::File getCacheDir() const;
    juce::File getVoiceListCacheFile() const;
    juce::File getVoiceSyxCacheFile(const juce::String& url) const;
    juce::File getVoiceJsonCacheFile(const juce::String& url) const;    std::function<void()> onClose; // Callback when window should be closed
    std::function<void(const std::vector<uint8_t>&)> onVoiceLoaded; // Callback when voice should be loaded into FM engine

private:
    juce::TextEditor searchBox;
    juce::ListBox voiceListBox;
    juce::ComboBox channelCombo;
    juce::TextButton editButton{"Edit"};
    juce::TextButton sendButton{"Send"};
    juce::Label statusLabel;
    juce::Label bankLabel;
    juce::Array<juce::var> voices;
    juce::Array<juce::var> filteredVoices;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceBrowserComponent)
};
