#include "MidiBrowserComponent.h"
#include <juce_core/juce_core.h>
#include <nlohmann/json.hpp>

static const juce::String kBrowseBaseUrl = "https://gifx.co/chip/browse?path=";
static const juce::String kSearchBaseUrl  = "https://gifx.co/chip/search?query=";
static const juce::String kMusicBaseUrl   = "https://gifx.co/music";

// DownloadThread: runs a single download on a background thread
class MidiBrowserComponent::DownloadThread : public juce::Thread {
public:
    DownloadThread() : juce::Thread("MidiBrowserDownload") {}

    // Set up a download task and start the thread
    void startDownload(const juce::String& url, const juce::File& destFile,
                       std::function<void(bool success)> callback) {
        downloadUrl  = url;
        destination  = destFile;
        onFinished   = std::move(callback);
        startThread();
    }

    void run() override {
        juce::URL url(downloadUrl);
        auto stream = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(10000));

        if (stream == nullptr || threadShouldExit()) {
            juce::MessageManager::callAsync([cb = onFinished]() { cb(false); });
            return;
        }

        juce::MemoryOutputStream mo;
        mo.writeFromInputStream(*stream, -1);

        if (threadShouldExit()) {
            juce::MessageManager::callAsync([cb = onFinished]() { cb(false); });
            return;
        }

        destination.getParentDirectory().createDirectory();
        if (!destination.replaceWithData(mo.getData(), mo.getDataSize())) {
            juce::MessageManager::callAsync([cb = onFinished]() { cb(false); });
            return;
        }

        juce::MessageManager::callAsync([cb = onFinished]() { cb(true); });
    }

private:
    juce::String downloadUrl;
    juce::File destination;
    std::function<void(bool)> onFinished;
};

// ======================== MidiBrowserComponent ========================

MidiBrowserComponent::MidiBrowserComponent() {
    setWantsKeyboardFocus(true);

    searchBox.setTextToShowWhenEmpty("Search MIDI files...", juce::Colours::grey);
    searchBox.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    searchBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff222222));
    searchBox.onReturnKey = [this] { doSearch(searchBox.getText()); };
    addAndMakeVisible(searchBox);

    breadcrumbLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    breadcrumbLabel.setText("/", juce::dontSendNotification);
    addAndMakeVisible(breadcrumbLabel);

    listBox.setModel(this);
    listBox.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff222222));
    listBox.setRowHeight(22);
    addAndMakeVisible(listBox);

    upButton.onClick = [this] {
        if (dirStack.size() > 0) {
            dirStack.remove(dirStack.size() - 1);
            juce::String path = dirStack.size() > 0 ? dirStack[dirStack.size() - 1] : "/";
            loadDirectory(path);
        }
    };
    addAndMakeVisible(upButton);

    loadButton.onClick = [this] {
        int row = listBox.getSelectedRow();
        if (row >= 0 && row < (int)entries.size() && !entries[row].isDirectory)
            downloadAndProcess(entries[row]);
    };
    addAndMakeVisible(loadButton);

    statusLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(statusLabel);

    filterBankButton.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    addAndMakeVisible(filterBankButton);

    loadDirectory("/");
}

MidiBrowserComponent::~MidiBrowserComponent() {
    if (downloadThread)
        downloadThread->stopThread(3000);
}

void MidiBrowserComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff2a2a2a));
}

void MidiBrowserComponent::resized() {
    auto area = getLocalBounds().reduced(6);

    // Top: search box
    auto topRow = area.removeFromTop(28);
    searchBox.setBounds(topRow);
    area.removeFromTop(4);

    // Breadcrumb
    breadcrumbLabel.setBounds(area.removeFromTop(20));
    area.removeFromTop(4);

    // Buttons row
    auto btnRow = area.removeFromTop(24);
    upButton.setBounds(btnRow.removeFromLeft(50));
    btnRow.removeFromLeft(4);
    loadButton.setBounds(btnRow.removeFromLeft(90));
    btnRow.removeFromLeft(4);
    filterBankButton.setBounds(btnRow);
    area.removeFromTop(4);

    // Status at bottom
    auto statusArea = area.removeFromBottom(20);
    statusLabel.setBounds(statusArea);
    area.removeFromBottom(4);

    listBox.setBounds(area);
}

// ---- ListBoxModel ----

int MidiBrowserComponent::getNumRows() {
    return (int)entries.size();
}

void MidiBrowserComponent::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) {
    if (row < 0 || row >= (int)entries.size()) return;

    if (selected)
        g.fillAll(juce::Colours::steelblue.darker());
    else
        g.fillAll(juce::Colour(0xff222222));

    const auto& e = entries[row];
    juce::String prefix = e.isDirectory ? "[DIR] " : "      ";
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(13.0f)));

    // Show just the last path component
    juce::String display = e.path;
    int slashPos = display.lastIndexOfChar('/');
    if (slashPos >= 0)
        display = display.substring(slashPos + 1);

    g.drawText(prefix + display, 4, 0, width - 8, height, juce::Justification::centredLeft, true);
}

void MidiBrowserComponent::listBoxItemDoubleClicked(int row, const juce::MouseEvent&) {
    if (row < 0 || row >= (int)entries.size()) return;
    const auto& e = entries[row];
    if (e.isDirectory) {
        loadDirectory(e.path);
    } else {
        downloadAndProcess(e);
    }
}

void MidiBrowserComponent::selectedRowsChanged(int /*lastRow*/) {}

// ---- Directory / Search ----

void MidiBrowserComponent::loadDirectory(const juce::String& path) {
    currentPath = path;
    breadcrumbLabel.setText(path, juce::dontSendNotification);
    setStatus("Loading...");

    juce::String encoded = juce::URL::addEscapeChars(path, false);
    juce::URL url(kBrowseBaseUrl + encoded);

    auto stream = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
        .withConnectionTimeoutMs(10000));

    if (!stream) {
        setStatus("Failed to connect.", true);
        return;
    }

    juce::String json = stream->readEntireStreamAsString();

    try {
        auto j = nlohmann::json::parse(json.toStdString());
        entries.clear();

        if (j.contains("items") && j["items"].is_array()) {
            for (auto& item : j["items"]) {
                BrowseEntry e;
                if (item.contains("file"))
                    e.path = juce::String(item["file"].get<std::string>());
                else if (item.contains("path"))
                    e.path = juce::String(item["path"].get<std::string>());
                e.isDirectory = item.contains("type") && item["type"] == "directory";
                if (e.path.isNotEmpty())
                    entries.push_back(e);
            }
        } else if (j.is_array()) {
            for (auto& item : j) {
                BrowseEntry e;
                if (item.contains("file"))
                    e.path = juce::String(item["file"].get<std::string>());
                else if (item.contains("path"))
                    e.path = juce::String(item["path"].get<std::string>());
                e.isDirectory = item.contains("type") && item["type"] == "directory";
                if (e.path.isNotEmpty())
                    entries.push_back(e);
            }
        }

        // Update dir stack
        if (dirStack.isEmpty() || dirStack[dirStack.size() - 1] != path)
            dirStack.add(path);

        listBox.updateContent();
        listBox.repaint();
        setStatus(juce::String(entries.size()) + " items");
    } catch (...) {
        setStatus("Parse error.", true);
    }
}

void MidiBrowserComponent::doSearch(const juce::String& query) {
    if (query.isEmpty()) { loadDirectory("/"); return; }

    setStatus("Searching...");
    juce::String encoded = juce::URL::addEscapeChars(query, true);
    juce::URL url(kSearchBaseUrl + encoded + "&limit=100");

    auto stream = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
        .withConnectionTimeoutMs(10000));

    if (!stream) { setStatus("Search failed.", true); return; }

    juce::String json = stream->readEntireStreamAsString();
    try {
        auto j = nlohmann::json::parse(json.toStdString());
        entries.clear();

        auto parseItems = [&](const nlohmann::json& arr) {
            for (auto& item : arr) {
                BrowseEntry e;
                if (item.contains("file"))
                    e.path = juce::String(item["file"].get<std::string>());
                else if (item.contains("path"))
                    e.path = juce::String(item["path"].get<std::string>());
                e.isDirectory = false;
                if (e.path.isNotEmpty())
                    entries.push_back(e);
            }
        };

        if (j.contains("items") && j["items"].is_array())
            parseItems(j["items"]);
        else if (j.is_array())
            parseItems(j);

        listBox.updateContent();
        listBox.repaint();
        setStatus(juce::String(entries.size()) + " results");
    } catch (...) {
        setStatus("Search parse error.", true);
    }
}

// ---- Download & Process ----

juce::File MidiBrowserComponent::getCacheDir() const {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("MiniDexed_Service_Utility/mid_cache");
}

juce::File MidiBrowserComponent::getCachedFile(const juce::String& url) const {
    return getCacheDir().getChildFile(
        juce::String::toHexString(url.hashCode64()) + ".mid");
}

void MidiBrowserComponent::setStatus(const juce::String& msg, bool error) {
    statusLabel.setColour(juce::Label::textColourId, error ? juce::Colours::orangered : juce::Colours::white);
    statusLabel.setText(msg, juce::dontSendNotification);
}

void MidiBrowserComponent::downloadAndProcess(const BrowseEntry& entry) {
    juce::String downloadUrl = kMusicBaseUrl + entry.path;
    juce::File cached = getCachedFile(downloadUrl);
    juce::String fileName = entry.path.fromLastOccurrenceOf("/", false, false);

    if (cached.existsAsFile()) {
        showTrackDialog(cached, fileName);
        return;
    }

    setStatus("Downloading...");

    if (downloadThread)
        downloadThread->stopThread(2000);

    downloadThread = std::make_unique<DownloadThread>();
    downloadThread->startDownload(downloadUrl, cached,
        [this, cached, fileName](bool success) {
            if (success)
                showTrackDialog(cached, fileName);
            else
                setStatus("Download failed.", true);
        });
}

void MidiBrowserComponent::showTrackDialog(const juce::File& cachedFile, const juce::String& fileName) {
    juce::FileInputStream fis(cachedFile);
    if (!fis.openedOk()) { setStatus("Cannot open file.", true); return; }

    juce::MidiFile midi;
    if (!midi.readFrom(fis)) { setStatus("Not a valid MIDI file.", true); return; }

    setStatus("Ready: " + fileName);

    // If MIDI has only 1 track (or 0 note-bearing tracks), skip dialog
    bool needsDialog = false;
    int noteTracks = 0;
    for (int t = 0; t < midi.getNumTracks(); ++t) {
        for (int i = 0; i < midi.getTrack(t)->getNumEvents(); ++i) {
            if (midi.getTrack(t)->getEventPointer(i)->message.isNoteOn()) { ++noteTracks; break; }
        }
    }
    needsDialog = noteTracks > 1;

    if (!needsDialog) {
        std::vector<TrackAssignment> assignments;
        for (int t = 0; t < midi.getNumTracks(); ++t) {
            TrackAssignment a;
            a.originalTrack = t;
            a.assignedChannel = 1;
            assignments.push_back(a);
        }
        processAndFire(midi, fileName, assignments);
        return;
    }

    trackDialogComp = std::make_unique<TrackChannelDialog>(midi);
    auto* compPtr = trackDialogComp.get();

    class TrackDialogWindow : public juce::DialogWindow {
    public:
        std::function<void()> onClose;
        TrackDialogWindow(const juce::String& t, juce::Component* c)
            : juce::DialogWindow(t, juce::Colour(0xff2a2a2a), true) {
            setContentNonOwned(c, true);
            setResizable(false, false);
            centreWithSize(420, std::min(80 + (int)c->getHeight(), 500));
            setVisible(true);
        }
        void closeButtonPressed() override { if (onClose) onClose(); }
    };

    juce::MidiFile midiCopy = midi;

    compPtr->onDone = [this, midiCopy, fileName](bool accepted) {
        if (accepted && trackDialogComp)
            processAndFire(midiCopy, fileName, trackDialogComp->getAssignments());
        if (trackDialogWindow)
            trackDialogWindow->setVisible(false);
        trackDialogWindow.reset();
        trackDialogComp.reset();
    };

    auto* win = new TrackDialogWindow("Assign Tracks", compPtr);
    win->onClose = [this]() { trackDialogWindow.reset(); trackDialogComp.reset(); };
    trackDialogWindow.reset(win);
}

void MidiBrowserComponent::processAndFire(const juce::MidiFile& midi, const juce::String& fileName,
                                           const std::vector<TrackAssignment>& assignments) {
    if (filterBankButton.getToggleState()) {
        // Build a new MidiFile with bank/program changes filtered out
        juce::MidiFile filtered;
        filtered.setTicksPerQuarterNote(midi.getTimeFormat());
        for (const auto& asn : assignments) {
            if (asn.assignedChannel == 0) continue;
            if (asn.originalTrack >= midi.getNumTracks()) continue;
            const auto* src = midi.getTrack(asn.originalTrack);
            juce::MidiMessageSequence seq;
            for (int i = 0; i < src->getNumEvents(); ++i) {
                const auto& msg = src->getEventPointer(i)->message;
                if (msg.isProgramChange() || msg.isController()) continue;
                auto m = msg;
                if (!m.isMetaEvent())
                    m.setChannel(asn.assignedChannel);
                seq.addEvent(m);
            }
            filtered.addTrack(seq);
        }
        if (onMidiReady) onMidiReady(filtered, fileName);
    } else {
        // Build a MidiFile with channel remapping applied
        juce::MidiFile remapped;
        remapped.setTicksPerQuarterNote(midi.getTimeFormat());
        for (const auto& asn : assignments) {
            if (asn.assignedChannel == 0) continue;
            if (asn.originalTrack >= midi.getNumTracks()) continue;
            const auto* src = midi.getTrack(asn.originalTrack);
            juce::MidiMessageSequence seq;
            for (int i = 0; i < src->getNumEvents(); ++i) {
                auto msg = src->getEventPointer(i)->message;
                if (!msg.isMetaEvent())
                    msg.setChannel(asn.assignedChannel);
                seq.addEvent(msg);
            }
            remapped.addTrack(seq);
        }
        if (onMidiReady) onMidiReady(remapped, fileName);
    }
}
