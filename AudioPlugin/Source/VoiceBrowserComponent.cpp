#include "VoiceBrowserComponent.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <functional>
#include <iostream>
#include "../../src/FMRack/VoiceData.h"

static const juce::String voiceListUrl = "https://patches.fm/patches/dx7/patch_list.json";
static const juce::String voiceListCacheName = "patch_list.json";

VoiceBrowserComponent::VoiceBrowserComponent() {
    // Enable key listening for ESC key
    setWantsKeyboardFocus(true);
    addKeyListener(this);
    
    addAndMakeVisible(searchBox);
    searchBox.setTextToShowWhenEmpty("Search voices...", juce::Colours::grey);
    searchBox.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    searchBox.setColour(juce::TextEditor::backgroundColourId, juce::Colours::black);
    searchBox.onTextChange = [this] { filterVoices(); };

    addAndMakeVisible(voiceListBox);
    voiceListBox.setModel(this);
    voiceListBox.setColour(juce::ListBox::backgroundColourId, juce::Colours::black);

    addAndMakeVisible(channelCombo);
    channelCombo.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    channelCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colours::black);
    for (int i = 1; i <= 16; ++i) channelCombo.addItem("MIDI " + juce::String(i), i);
    channelCombo.addItem("Omni", 17);
    channelCombo.setSelectedId(1);    addAndMakeVisible(editButton);
    addAndMakeVisible(sendButton);
    editButton.setEnabled(false);
    sendButton.setEnabled(false);
    
    editButton.setButtonText("Edit");
    sendButton.setButtonText("Load");
    
    editButton.onClick = [this] {
        int idx = voiceListBox.getSelectedRow();
        if (idx >= 0 && idx < filteredVoices.size()) downloadVoiceSyx(idx);
    };
    sendButton.onClick = [this] {
        int idx = voiceListBox.getSelectedRow();
        if (idx >= 0 && idx < filteredVoices.size()) downloadVoiceSyx(idx);
    };

    addAndMakeVisible(statusLabel);
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(bankLabel);
    bankLabel.setColour(juce::Label::textColourId, juce::Colours::white);

    loadVoiceList();
}

VoiceBrowserComponent::~VoiceBrowserComponent() {
    removeKeyListener(this);
}

void VoiceBrowserComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::darkgrey);
}

void VoiceBrowserComponent::resized() {
    auto area = getLocalBounds().reduced(8);
    searchBox.setBounds(area.removeFromTop(24));
    voiceListBox.setBounds(area.removeFromTop(200));
    auto row = area.removeFromTop(24);
    channelCombo.setBounds(row.removeFromLeft(100));
    editButton.setBounds(row.removeFromLeft(60));
    sendButton.setBounds(row.removeFromLeft(60));
    statusLabel.setBounds(area.removeFromTop(20));
    bankLabel.setBounds(area.removeFromTop(20));
}

bool VoiceBrowserComponent::keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) {
    if (key == juce::KeyPress::escapeKey) {
        if (onClose) onClose();
        return true;
    }
    return false;
}

int VoiceBrowserComponent::getNumRows() {
    return filteredVoices.size();
}

void VoiceBrowserComponent::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) {
    if (row < 0 || row >= filteredVoices.size()) return;
    auto v = filteredVoices[row];
    juce::String name = v["name"].toString();
    juce::String author = v.getProperty("author", "").toString();
    if (selected) g.fillAll(juce::Colours::lightblue);
    g.setColour(selected ? juce::Colours::black : juce::Colours::white);
    g.drawText(name + " - " + author, 4, 0, width-8, height, juce::Justification::centredLeft);
}

void VoiceBrowserComponent::listBoxItemClicked(int row, const juce::MouseEvent&) {
    voiceListBox.selectRow(row);
}

void VoiceBrowserComponent::setStatus(const juce::String& msg, bool error) {
    statusLabel.setText(msg, juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, error ? juce::Colours::red : juce::Colours::white);
}

juce::File VoiceBrowserComponent::getCacheDir() const {
    auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    return appData.getChildFile("MiniDexed_Service_Utility/patches_cache");
}
juce::File VoiceBrowserComponent::getVoiceListCacheFile() const {
    return getCacheDir().getChildFile(voiceListCacheName);
}
juce::File VoiceBrowserComponent::getVoiceSyxCacheFile(const juce::String& url) const {
    auto hash = juce::String(juce::String::toHexString(juce::String(url).hashCode64()));
    return getCacheDir().getChildFile(hash + ".syx");
}
juce::File VoiceBrowserComponent::getVoiceJsonCacheFile(const juce::String& url) const {
    auto hash = juce::String(juce::String::toHexString(juce::String(url).hashCode64()));
    return getCacheDir().getChildFile(hash + ".json");
}

void VoiceBrowserComponent::loadVoiceList() {
    std::cerr << "[VoiceBrowser] Starting voice list loading..." << std::endl;
    
    // First, ensure cache directory exists
    auto cacheDir = getCacheDir();
    std::cerr << "[VoiceBrowser] Cache directory: " << cacheDir.getFullPathName().toStdString() << std::endl;
    if (!cacheDir.exists()) {
        std::cerr << "[VoiceBrowser] Creating cache directory..." << std::endl;
        auto result = cacheDir.createDirectory();
        if (result.failed()) {
            std::cerr << "[VoiceBrowser] Failed to create cache directory: " << result.getErrorMessage().toStdString() << std::endl;
            setStatus("Failed to create cache directory: " + result.getErrorMessage(), true);
            return;
        }
        std::cerr << "[VoiceBrowser] Cache directory created successfully" << std::endl;
    }
    
    auto cacheFile = getVoiceListCacheFile();
    std::cerr << "[VoiceBrowser] Cache file path: " << cacheFile.getFullPathName().toStdString() << std::endl;
    
    if (cacheFile.existsAsFile()) {
        std::cerr << "[VoiceBrowser] Loading from cache file..." << std::endl;
        juce::String json = cacheFile.loadFileAsString();
        std::cerr << "[VoiceBrowser] Cache file size: " << json.length() << " characters" << std::endl;
        parseVoiceList(json);
        return;
    }
    
    std::cerr << "[VoiceBrowser] Cache not found, downloading from: " << voiceListUrl.toStdString() << std::endl;
    juce::URL url(voiceListUrl);
    
    setStatus("Downloading voice list...");
    std::unique_ptr<juce::InputStream> stream(url.createInputStream(false));
    if (stream != nullptr) {
        std::cerr << "[VoiceBrowser] Download stream created successfully" << std::endl;
        juce::String json = stream->readEntireStreamAsString();
        std::cerr << "[VoiceBrowser] Downloaded " << json.length() << " characters" << std::endl;
        
        if (json.isNotEmpty()) {
            std::cerr << "[VoiceBrowser] Saving to cache..." << std::endl;
            if (cacheFile.replaceWithText(json)) {
                std::cerr << "[VoiceBrowser] Cached successfully" << std::endl;
            } else {
                std::cerr << "[VoiceBrowser] Failed to write cache file" << std::endl;
            }
            parseVoiceList(json);
        } else {
            std::cerr << "[VoiceBrowser] Downloaded empty content" << std::endl;
            setStatus("Downloaded empty voice list", true);
        }
    } else {
        std::cerr << "[VoiceBrowser] Failed to create input stream for URL" << std::endl;
        setStatus("Failed to download patch list", true);
    }
}

void VoiceBrowserComponent::parseVoiceList(const juce::String& json) {
    std::cerr << "[VoiceBrowser] Parsing voice list JSON with nlohmann/json..." << std::endl;
    std::cerr << "[VoiceBrowser] JSON length: " << json.length() << " characters" << std::endl;
    
    if (json.isEmpty()) {
        std::cerr << "[VoiceBrowser] JSON is empty" << std::endl;
        setStatus("Empty voice list received", true);
        return;
    }
    
    setStatus("Parsing voice list...");
    
    try {
        // Convert JUCE String to std::string for nlohmann/json
        std::string jsonString = json.toStdString();
        
        std::cerr << "[VoiceBrowser] Starting nlohmann/json parse..." << std::endl;
        
        // Parse with nlohmann/json (much faster and more robust)
        nlohmann::json jsonData = nlohmann::json::parse(jsonString);
        
        std::cerr << "[VoiceBrowser] JSON parsed successfully" << std::endl;
        
        if (!jsonData.is_array()) {
            std::cerr << "[VoiceBrowser] JSON is not an array" << std::endl;
            setStatus("Voice list is not an array", true);
            return;
        }
        
        // Clear and rebuild voices array
        voices.clear();
        
        // Convert nlohmann::json objects to juce::var objects
        int count = 0;
        for (const auto& voiceJson : jsonData) {
            if (!voiceJson.is_object()) continue;
            
            // Create a JUCE var object for compatibility with existing code
            juce::DynamicObject::Ptr voiceObj = new juce::DynamicObject();
            
            // Extract common fields
            if (voiceJson.contains("name") && voiceJson["name"].is_string()) {
                voiceObj->setProperty("name", juce::String(voiceJson["name"].get<std::string>()));
            }
            if (voiceJson.contains("author") && voiceJson["author"].is_string()) {
                voiceObj->setProperty("author", juce::String(voiceJson["author"].get<std::string>()));
            }
            if (voiceJson.contains("signature") && voiceJson["signature"].is_string()) {
                voiceObj->setProperty("signature", juce::String(voiceJson["signature"].get<std::string>()));
            }
            if (voiceJson.contains("manufacturer") && voiceJson["manufacturer"].is_string()) {
                voiceObj->setProperty("manufacturer", juce::String(voiceJson["manufacturer"].get<std::string>()));
            }
            if (voiceJson.contains("model") && voiceJson["model"].is_string()) {
                voiceObj->setProperty("model", juce::String(voiceJson["model"].get<std::string>()));
            }
            if (voiceJson.contains("source_bank") && voiceJson["source_bank"].is_string()) {
                voiceObj->setProperty("source_bank", juce::String(voiceJson["source_bank"].get<std::string>()));
            }
            
            voices.add(juce::var(voiceObj.get()));
            count++;
            
            // Progress feedback for very large datasets
            if (count % 1000 == 0) {
                std::cerr << "[VoiceBrowser] Processed " << count << " voices..." << std::endl;
                setStatus("Processing voices: " + juce::String(count) + "/" + juce::String((int)jsonData.size()));
            }
        }
        
        std::cerr << "[VoiceBrowser] Successfully loaded " << voices.size() << " voices" << std::endl;
        
        // Sample a few voices to verify structure
        if (voices.size() > 0) {
            auto firstVoice = voices[0];
            juce::String name = firstVoice["name"].toString();
            juce::String author = firstVoice.getProperty("author", "").toString();
            std::cerr << "[VoiceBrowser] First voice: '" << name.toStdString() << "' by '" << author.toStdString() << "'" << std::endl;
        }
        
        setStatus("Loaded " + juce::String(voices.size()) + " voices");
        filterVoices();
        
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "[VoiceBrowser] JSON parse error: " << e.what() << std::endl;
        std::cerr << "[VoiceBrowser] Error at byte position: " << e.byte << std::endl;
        setStatus("JSON parse error: " + juce::String(e.what()), true);
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "[VoiceBrowser] JSON exception: " << e.what() << std::endl;
        setStatus("JSON error: " + juce::String(e.what()), true);
    } catch (const std::exception& e) {
        std::cerr << "[VoiceBrowser] Standard exception: " << e.what() << std::endl;
        setStatus("Parse error: " + juce::String(e.what()), true);
    } catch (...) {
        std::cerr << "[VoiceBrowser] Unknown exception during JSON parsing" << std::endl;
        setStatus("Unknown parsing error", true);
    }
}

void VoiceBrowserComponent::filterVoices() {
    juce::String query = searchBox.getText().trim().toLowerCase();
    filteredVoices.clear();
    for (auto& v : voices) {
        juce::String name = v["name"].toString().toLowerCase();
        juce::String author = v.getProperty("author", "").toString().toLowerCase();
        if (name.contains(query) || author.contains(query))
            filteredVoices.add(v);
    }
    voiceListBox.updateContent();
    voiceListBox.repaint();
    setStatus("Filtered: " + juce::String(filteredVoices.size()) + " voices");
}

void VoiceBrowserComponent::downloadVoiceSyx(int index) {
    if (index < 0 || index >= filteredVoices.size()) return;
    auto v = filteredVoices[index];
    juce::String sig = v["signature"].toString();
    if (sig.isEmpty()) { setStatus("No signature for voice", true); return; }
    juce::String url = "https://patches.fm/patches/single-voice/dx7/" + sig.substring(0,2) + "/" + sig + ".syx";
    auto cacheFile = getVoiceSyxCacheFile(url);
    if (cacheFile.existsAsFile()) {
        juce::MemoryBlock syx;
        cacheFile.loadFileAsData(syx);
        setStatus("Loaded syx from cache: " + v["name"].toString() + " (" + juce::String(syx.getSize()) + " bytes)");
        int midiChannel = channelCombo.getSelectedId();
        sendVoiceSyx(syx, midiChannel);
        return;
    }
    juce::URL juceUrl(url);
    std::unique_ptr<juce::InputStream> stream(juceUrl.createInputStream(false));
    if (stream != nullptr) {
        juce::MemoryBlock syx;
        stream->readIntoMemoryBlock(syx);
        cacheFile.create();
        cacheFile.replaceWithData(syx.getData(), syx.getSize());
        setStatus("Downloaded syx: " + v["name"].toString() + " (" + juce::String(syx.getSize()) + " bytes)");
        int midiChannel = channelCombo.getSelectedId();
        sendVoiceSyx(syx, midiChannel);
    } else {
        setStatus("Failed to download syx", true);
    }
}

void VoiceBrowserComponent::sendVoiceSyx(const juce::MemoryBlock& syx, int midiChannel) {
    // Convert the .syx file data to DX7 voice format if needed
    std::vector<uint8_t> dx7Voice;
    
    if (syx.getSize() == 155) {
        // Raw voice data (155 bytes) - copy directly
        dx7Voice.assign(static_cast<const uint8_t*>(syx.getData()), 
                       static_cast<const uint8_t*>(syx.getData()) + 155);
    } else if (syx.getSize() == 163) {
        // DX7 single voice SysEx format: F0 43 00 00 01 1B ... 155 bytes ... checksum F7
        // Extract the 155 bytes of voice data (skip header and checksum)
        if (static_cast<const uint8_t*>(syx.getData())[0] == 0xF0 && 
            static_cast<const uint8_t*>(syx.getData())[1] == 0x43 &&
            static_cast<const uint8_t*>(syx.getData())[5] == 0x1B) {
            dx7Voice.assign(static_cast<const uint8_t*>(syx.getData()) + 6, 
                           static_cast<const uint8_t*>(syx.getData()) + 6 + 155);
        } else {
            setStatus("Invalid SysEx format", true);
            return;
        }
    } else {
        setStatus("Unsupported voice data format (size: " + juce::String((int)syx.getSize()) + " bytes)", true);
        return;
    }
    
    // Convert to Dexed format (156 bytes) if needed
    std::vector<uint8_t> dexedVoice;
    if (dx7Voice.size() == 155) {
        // Already in Dexed format
        dexedVoice = dx7Voice;
        dexedVoice.resize(156, 0); // Ensure 156 bytes
    } else if (dx7Voice.size() == 128) {
        // Convert from DX7 packed format to Dexed format
        dexedVoice = VoiceData::convertDX7ToDexed(dx7Voice);
    } else {
        setStatus("Invalid voice data size: " + juce::String((int)dx7Voice.size()), true);
        return;
    }
    
    // Load the voice using the same method as the "Open" button
    if (onVoiceLoaded) {
        onVoiceLoaded(dexedVoice);
        juce::String voiceName = VoiceData::extractDX7VoiceName(dexedVoice);
        setStatus("Voice loaded: " + voiceName);
    } else {
        setStatus("No voice loading callback available", true);
    }
}

void VoiceBrowserComponent::downloadBankInfo(int index) {
    if (index < 0 || index >= filteredVoices.size()) return;
    auto v = filteredVoices[index];
    juce::String sig = v["signature"].toString();
    if (sig.isEmpty()) { bankLabel.setText("", juce::dontSendNotification); return; }
    juce::String url = "https://patches.fm/patches/dx7/" + sig.substring(0,2) + "/" + sig + ".json";
    auto cacheFile = getVoiceJsonCacheFile(url);
    if (cacheFile.existsAsFile()) {
        juce::FileInputStream fis(cacheFile);
        juce::var json = juce::JSON::parse(fis.readEntireStreamAsString());
        juce::String bank = json["BANK"].toString();
        juce::String author = json["AUTHOR"].toString();
        bankLabel.setText("Source: Bank " + bank + " by " + author, juce::dontSendNotification);
        return;
    }
    juce::URL juceUrl(url);
    std::unique_ptr<juce::InputStream> stream(juceUrl.createInputStream(false));
    if (stream != nullptr) {
        juce::String result = stream->readEntireStreamAsString();
        juce::var json = juce::JSON::parse(result);
        if (json.isObject()) {
            cacheFile.create();
            cacheFile.replaceWithText(result);
            juce::String bank = json["BANK"].toString();
            juce::String author = json["AUTHOR"].toString();
            bankLabel.setText("Source: Bank " + bank + " by " + author, juce::dontSendNotification);
        } else {
            bankLabel.setText("Source: (not available)", juce::dontSendNotification);
        }
    } else {
        bankLabel.setText("Source: (not available)", juce::dontSendNotification);
    }
}

void VoiceBrowserComponent::selectedRowsChanged(int lastRowSelected) {
    bool hasSel = voiceListBox.getSelectedRow() >= 0;
    editButton.setEnabled(hasSel);
    sendButton.setEnabled(hasSel);
    if (hasSel) downloadBankInfo(voiceListBox.getSelectedRow());
}
