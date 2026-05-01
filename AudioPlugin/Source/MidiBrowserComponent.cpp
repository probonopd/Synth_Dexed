#include "MidiBrowserComponent.h"
#include <nlohmann/json.hpp>

// Helper to change MIDI channel on a channel message
static juce::MidiMessage withChannel(const juce::MidiMessage& msg, int newChannel)
{
    const uint8_t* raw = msg.getRawData();
    int size = msg.getRawDataSize();
    if (size < 1) return msg;
    std::vector<uint8_t> newData(raw, raw + size);
    newData[0] = static_cast<uint8_t>((newData[0] & 0xF0u) | (static_cast<uint8_t>(newChannel - 1) & 0x0Fu));
    return juce::MidiMessage(newData.data(), size, msg.getTimeStamp());
}

MidiBrowserComponent::MidiBrowserComponent()
{
    searchBox.setTextToShowWhenEmpty("Search MIDI files...", juce::Colours::grey);
    searchBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff222222));
    searchBox.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    searchBox.onReturnKey = [this]() { doSearch(searchBox.getText()); };
    addAndMakeVisible(searchBox);

    breadcrumbLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    breadcrumbLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
    addAndMakeVisible(breadcrumbLabel);

    listBox.setModel(this);
    listBox.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff1e1e1e));
    addAndMakeVisible(listBox);

    upButton.onClick = [this]()
    {
        if (dirStack.size() > 0)
        {
            juce::String parent = dirStack[dirStack.size() - 1];
            dirStack.remove(dirStack.size() - 1);
            loadDirectory(parent);
        }
        else
        {
            loadDirectory("/");
        }
    };
    addAndMakeVisible(upButton);

    sendButton.onClick = [this]()
    {
        int selected = listBox.getSelectedRow();
        if (selected >= 0)
            onItemActivated(selected);
    };
    addAndMakeVisible(sendButton);

    statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    statusLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
    addAndMakeVisible(statusLabel);

    replaceGMButton.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    addAndMakeVisible(replaceGMButton);

    filterBankButton.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    addAndMakeVisible(filterBankButton);

    loadDirectory("/");
}

MidiBrowserComponent::~MidiBrowserComponent()
{
    if (downloadJob)
    {
        downloadJob->stopThread(2000);
        downloadJob.reset();
    }
}

juce::File MidiBrowserComponent::getCacheDir() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("MiniDexed_Service_Utility/mid_cache");
}

juce::File MidiBrowserComponent::getCachedMidiFile(const juce::String& url) const
{
    return getCacheDir().getChildFile(
        juce::String::toHexString(juce::String(url).hashCode64()) + ".mid");
}

void MidiBrowserComponent::loadDirectory(const juce::String& path)
{
    currentPath = path;
    breadcrumbLabel.setText(path, juce::dontSendNotification);
    entries.clear();

    juce::String encodedPath = juce::URL::addEscapeChars(path, true);
    juce::String apiUrl = "https://gifx.co/chip/browse?path=" + encodedPath;

    auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                       .withConnectionTimeoutMs(5000);

    std::unique_ptr<juce::InputStream> stream(juce::URL(apiUrl).createInputStream(options));

    if (stream == nullptr)
    {
        setStatus("Failed to connect to server.", true);
        listBox.updateContent();
        return;
    }

    juce::String json = stream->readEntireStreamAsString();

    try
    {
        auto j = nlohmann::json::parse(json.toStdString());

        if (j.is_array())
        {
            std::vector<BrowseEntry> dirs, files;
            for (const auto& item : j)
            {
                if (!item.contains("type") || !item.contains("path"))
                    continue;

                std::string type = item["type"].get<std::string>();
                std::string itemPath = item["path"].get<std::string>();
                juce::String juceItemPath(itemPath.c_str());

                if (type == "directory")
                    dirs.push_back({ juceItemPath, true });
                else if (type == "file")
                {
                    juce::String lower = juceItemPath.toLowerCase();
                    if (lower.endsWith(".mid") || lower.endsWith(".midi"))
                        files.push_back({ juceItemPath, false });
                }
            }

            for (auto& d : dirs) entries.push_back(d);
            for (auto& f : files) entries.push_back(f);
        }
    }
    catch (const std::exception& e)
    {
        setStatus("Parse error: " + juce::String(e.what()), true);
    }

    listBox.updateContent();
    listBox.repaint();
    setStatus("Loaded " + juce::String((int)entries.size()) + " items");
}

void MidiBrowserComponent::doSearch(const juce::String& query)
{
    if (query.isEmpty()) return;
    entries.clear();

    juce::String encodedQuery = juce::URL::addEscapeChars(query, true);
    juce::String apiUrl = "https://gifx.co/chip/search?query=" + encodedQuery + "&limit=100";

    auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                       .withConnectionTimeoutMs(5000);

    std::unique_ptr<juce::InputStream> stream(juce::URL(apiUrl).createInputStream(options));

    if (stream == nullptr)
    {
        setStatus("Search failed: could not connect.", true);
        listBox.updateContent();
        return;
    }

    juce::String json = stream->readEntireStreamAsString();

    try
    {
        auto j = nlohmann::json::parse(json.toStdString());

        auto processItem = [&](const nlohmann::json& item)
        {
            std::string itemPath;
            if (item.contains("path"))
                itemPath = item["path"].get<std::string>();
            else if (item.contains("file"))
                itemPath = item["file"].get<std::string>();
            else
                return;

            juce::String juceItemPath(itemPath.c_str());
            juce::String lower = juceItemPath.toLowerCase();
            if (lower.endsWith(".mid") || lower.endsWith(".midi"))
                entries.push_back({ juceItemPath, false });
        };

        if (j.is_array())
        {
            for (const auto& item : j)
                processItem(item);
        }
        else if (j.is_object() && j.contains("items"))
        {
            for (const auto& item : j["items"])
                processItem(item);
        }
    }
    catch (const std::exception& e)
    {
        setStatus("Search parse error: " + juce::String(e.what()), true);
    }

    breadcrumbLabel.setText("Search: " + query, juce::dontSendNotification);
    listBox.updateContent();
    listBox.repaint();
    setStatus("Found " + juce::String((int)entries.size()) + " results");
}

void MidiBrowserComponent::downloadMidiAsync(const juce::String& url,
                                              const juce::String& fileName,
                                              std::function<void(juce::File, juce::String)> callback)
{
    juce::File cacheFile = getCachedMidiFile(url);

    if (cacheFile.existsAsFile())
    {
        juce::MessageManager::callAsync([callback, cacheFile, fileName]()
        {
            callback(cacheFile, fileName);
        });
        return;
    }

    getCacheDir().createDirectory();

    if (downloadJob)
    {
        downloadJob->stopThread(2000);
        downloadJob.reset();
    }

    downloadJob = std::make_unique<DownloadJob>(url, fileName, cacheFile, callback);
    downloadJob->startThread();
}

void MidiBrowserComponent::DownloadJob::run()
{
    auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                       .withConnectionTimeoutMs(10000);

    std::unique_ptr<juce::InputStream> stream(juce::URL(url).createInputStream(options));

    if (stream == nullptr || threadShouldExit())
    {
        juce::MessageManager::callAsync([this]() { callback(juce::File(), fileName); });
        return;
    }

    juce::MemoryBlock data;
    stream->readIntoMemoryBlock(data);

    if (threadShouldExit())
        return;

    destFile.getParentDirectory().createDirectory();
    juce::FileOutputStream fos(destFile);
    if (fos.openedOk())
        fos.write(data.getData(), data.getSize());
    fos.flush();

    auto cb = callback;
    auto dest = destFile;
    auto name = fileName;
    juce::MessageManager::callAsync([cb, dest, name]() { cb(dest, name); });
}

void MidiBrowserComponent::showTrackChannelDialog(const juce::File& midiFile,
                                                   const juce::String& fileName)
{
    juce::FileInputStream fis(midiFile);
    if (!fis.openedOk())
    {
        setStatus("Could not open MIDI file.", true);
        return;
    }

    // Load original (tick-based) for processing
    juce::MidiFile originalMidi;
    if (!originalMidi.readFrom(fis))
    {
        setStatus("Failed to parse MIDI file.", true);
        return;
    }

    auto* dlgComp = new TrackChannelDialog(originalMidi);
    trackChannelComp.reset(dlgComp);

    // Capture what we need for the lambda
    juce::MidiFile capturedOriginal = originalMidi;
    bool doFilterBank = filterBankButton.getToggleState();

    trackChannelComp->onDone = [this, capturedOriginal, fileName, doFilterBank](bool accepted) mutable
    {
        if (trackDialog)
            trackDialog->exitModalState(0);

        if (!accepted)
        {
            trackChannelComp.reset();
            trackDialog.reset();
            return;
        }

        // Collect assignments
        auto assignments = trackChannelComp->getAssignments();
        trackChannelComp.reset();
        trackDialog.reset();

        // Build new MIDI file with channel assignments (tick-based)
        juce::MidiFile outFile;
        outFile.setTicksPerQuarterNote(capturedOriginal.getTimeFormat());

        for (const auto& assignment : assignments)
        {
            if (assignment.assignedChannel == 0)
                continue; // Skip this track

            const juce::MidiMessageSequence* srcSeq = capturedOriginal.getTrack(assignment.originalTrack);
            if (srcSeq == nullptr) continue;

            juce::MidiMessageSequence newSeq;
            int newChannel = assignment.assignedChannel;

            for (int i = 0; i < srcSeq->getNumEvents(); ++i)
            {
                const auto* holder = srcSeq->getEventPointer(i);
                if (!holder) continue;

                juce::MidiMessage msg = holder->message;

                if (msg.isNoteOn() || msg.isNoteOff() || msg.isController() ||
                    msg.isPitchWheel() || msg.isChannelPressure() || msg.isAftertouch() ||
                    msg.isProgramChange())
                {
                    if (doFilterBank && (msg.isProgramChange() ||
                        (msg.isController() && (msg.getControllerNumber() == 0 ||
                                                 msg.getControllerNumber() == 32))))
                        continue;

                    msg = withChannel(msg, newChannel);
                }

                newSeq.addEvent(msg);
            }

            newSeq.updateMatchedPairs();
            outFile.addTrack(newSeq);
        }

        // Save modified file
        getCacheDir().createDirectory();
        juce::File modFile = getCacheDir().getChildFile(fileName + ".modified.mid");
        {
            juce::FileOutputStream fos(modFile);
            if (fos.openedOk())
                outFile.writeTo(fos);
        }

        lastModifiedPath = modFile.getFullPathName();

        // Load modified file and convert to seconds for playback
        juce::FileInputStream fisPlayback(modFile);
        juce::MidiFile playbackMidi;
        if (fisPlayback.openedOk() && playbackMidi.readFrom(fisPlayback))
        {
            if (onMidiReady)
                onMidiReady(playbackMidi, fileName);
        }
        else
        {
            setStatus("Failed to load modified MIDI.", true);
        }
    };

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setNonOwned(trackChannelComp.get());
    opts.dialogTitle = "Assign MIDI Tracks to Channels";
    opts.dialogBackgroundColour = juce::Colours::darkgrey;
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = false;
    opts.resizable = false;
    trackDialog.reset(opts.launchAsync());
}

void MidiBrowserComponent::onItemActivated(int row)
{
    if (row < 0 || row >= static_cast<int>(entries.size()))
        return;

    const BrowseEntry& entry = entries[static_cast<size_t>(row)];

    if (entry.isDirectory)
    {
        dirStack.add(currentPath);
        loadDirectory(entry.path);
    }
    else
    {
        // Build download URL
        juce::String downloadUrl = "https://gifx.co/music" + entry.path;
        juce::String baseName = juce::File(entry.path).getFileName();

        setStatus("Downloading: " + baseName + "...");

        downloadMidiAsync(downloadUrl, baseName, [this](juce::File file, juce::String name)
        {
            if (!file.existsAsFile())
            {
                setStatus("Download failed.", true);
                return;
            }
            setStatus("Downloaded: " + name);
            showTrackChannelDialog(file, name);
        });
    }
}

int MidiBrowserComponent::getNumRows()
{
    return static_cast<int>(entries.size());
}

void MidiBrowserComponent::paintListBoxItem(int row, juce::Graphics& g,
                                             int width, int height, bool selected)
{
    if (selected)
        g.fillAll(juce::Colour(0xff3a5a8a));
    else
        g.fillAll(juce::Colour(0xff1e1e1e));

    if (row < 0 || row >= static_cast<int>(entries.size()))
        return;

    const BrowseEntry& entry = entries[static_cast<size_t>(row)];
    juce::String displayName = entry.path;

    // Show just the last path component
    int lastSlash = displayName.lastIndexOf("/");
    if (lastSlash >= 0)
        displayName = displayName.substring(lastSlash + 1);

    if (entry.isDirectory)
        displayName = juce::String(juce::CharPointer_UTF8("\xf0\x9f\x93\x81")) + " " + displayName;

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawText(displayName, 6, 0, width - 6, height, juce::Justification::centredLeft);
}

void MidiBrowserComponent::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    onItemActivated(row);
}

void MidiBrowserComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a1a));
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    g.drawText("MIDI Browser", getLocalBounds().removeFromTop(20), juce::Justification::centred);
}

void MidiBrowserComponent::resized()
{
    auto area = getLocalBounds().reduced(4);

    // Top row: search + up button
    auto topRow = area.removeFromTop(28);
    upButton.setBounds(topRow.removeFromRight(40));
    topRow.removeFromRight(4);
    searchBox.setBounds(topRow);
    area.removeFromTop(2);

    // Breadcrumb
    breadcrumbLabel.setBounds(area.removeFromTop(18));
    area.removeFromTop(2);

    // Bottom controls
    auto bottomArea = area.removeFromBottom(80);
    bottomArea.removeFromTop(4);

    auto buttonRow = bottomArea.removeFromTop(24);
    sendButton.setBounds(buttonRow.removeFromLeft(90));
    bottomArea.removeFromTop(4);

    replaceGMButton.setBounds(bottomArea.removeFromTop(20));
    bottomArea.removeFromTop(2);
    filterBankButton.setBounds(bottomArea.removeFromTop(20));
    bottomArea.removeFromTop(2);
    statusLabel.setBounds(bottomArea.removeFromTop(16));

    // List box fills remaining space
    listBox.setBounds(area);
}

void MidiBrowserComponent::setStatus(const juce::String& msg, bool error)
{
    statusLabel.setText(msg, juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId,
                          error ? juce::Colours::red : juce::Colours::lightgrey);
}
