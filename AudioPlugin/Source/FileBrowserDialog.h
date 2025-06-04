#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>
#include <functional>

class CustomDialogWindow;

class FileBrowserDialog : public juce::Component,
                         public juce::FileBrowserListener,
                         public juce::KeyListener,
                         public juce::Button::Listener
{
public:
    enum class DialogType { Voice, Performance, Other };
    FileBrowserDialog(const juce::String& title = {},
                     const juce::String& filePattern = "*",
                     const juce::File& initialDirectory = juce::File::getCurrentWorkingDirectory(),
                     DialogType dialogType = DialogType::Other);
    
    ~FileBrowserDialog() override;
    
    void showDialog(juce::Component* parent,
                   std::function<void(const juce::File&)> onFileSelected,
                   std::function<void()> onCancelled = nullptr);
    
    void fileDoubleClicked(const juce::File& file) override;
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;
    void resized() override;
    void closeDialog();
    
    void buttonClicked(juce::Button* button) override;
    
    // Implement required pure virtual methods from juce::FileBrowserListener
    void selectionChanged() override {}
    void browserRootChanged(const juce::File&) override {}
    void fileClicked(const juce::File&, const juce::MouseEvent&) override;
    
    friend class CustomDialogWindow;
    
private:
    void cancelButtonClicked();
    
    static juce::File getLastDirectory(DialogType type);
    static void setLastDirectory(DialogType type, const juce::File& dir);
    
    std::unique_ptr<juce::FileBrowserComponent> fileBrowser;
    std::unique_ptr<juce::TextButton> cancelButton;
    std::unique_ptr<juce::DialogWindow> dialogWindow;
    
    juce::String filePattern;
    DialogType dialogType = DialogType::Other;
    std::function<void(const juce::File&)> fileSelectedCallback;
    std::function<void()> cancelledCallback;
    std::function<void()> cancelButtonCallback;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FileBrowserDialog)
};