#include "FileBrowserDialog.h"

/* The native Windows file open dialog causes a 5 second stalling after the dialog closes, hence we are using our own */

class CustomDialogWindow : public juce::DialogWindow
{
public:
    CustomDialogWindow(const juce::String& name, juce::Colour backgroundColour, bool escapeKeyTriggersCloseButton, FileBrowserDialog* owner)
        : juce::DialogWindow(name, backgroundColour, escapeKeyTriggersCloseButton), dialogOwner(owner)
    {
    }
    
    void closeButtonPressed() override
    {
        if (dialogOwner)
            dialogOwner->closeDialog();
    }
    
private:
    FileBrowserDialog* dialogOwner;
};

FileBrowserDialog::FileBrowserDialog(const juce::String&,
                                   const juce::String& filePattern,
                                   const juce::File& initialDirectory)
    : filePattern(filePattern)
{
    fileBrowser = std::make_unique<juce::FileBrowserComponent>(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        initialDirectory,
        nullptr,
        nullptr);

    fileBrowser->addListener(this);
    addAndMakeVisible(fileBrowser.get());

    cancelButton = std::make_unique<juce::TextButton>("Cancel");
    cancelButton->addListener(this);
    addAndMakeVisible(cancelButton.get());
}

FileBrowserDialog::~FileBrowserDialog()
{
    if (fileBrowser)
        fileBrowser->removeListener(this);
    if (cancelButton)
        cancelButton->removeListener(this);
}

void FileBrowserDialog::showDialog(juce::Component* parent,
                                 std::function<void(const juce::File&)> onFileSelected,
                                 std::function<void()> onCancelled)
{
    fileSelectedCallback = onFileSelected;
    cancelledCallback = onCancelled;
    
    setSize(600, 400);
    
    dialogWindow = std::make_unique<CustomDialogWindow>("Select File", juce::Colours::darkgrey, true, this);
    dialogWindow->setContentOwned(this, false);
    dialogWindow->setUsingNativeTitleBar(true);
    dialogWindow->centreAroundComponent(parent, getWidth(), getHeight());
    dialogWindow->setVisible(true);
    dialogWindow->setResizable(true, false);
    
    // Add key listener to handle Escape key
    addKeyListener(this);
    setWantsKeyboardFocus(true);
    grabKeyboardFocus();
}

void FileBrowserDialog::fileDoubleClicked(const juce::File& file)
{
    if (file.existsAsFile())
    {
        if (fileSelectedCallback)
            fileSelectedCallback(file);
        closeDialog();
    }
}

bool FileBrowserDialog::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    if (key == juce::KeyPress::escapeKey)
    {
        closeDialog();
        return true;
    }
    return false;
}

void FileBrowserDialog::resized()
{
    fileBrowser->setBounds(0, 0, getWidth(), getHeight() - 50);
    cancelButton->setBounds(10, getHeight() - 40, 100, 30);
}

void FileBrowserDialog::closeDialog()
{
    removeKeyListener(this);
    
    if (cancelledCallback)
        cancelledCallback();
        
    if (dialogWindow)
    {
        dialogWindow->setVisible(false);
        dialogWindow.reset();
    }
}

void FileBrowserDialog::cancelButtonClicked()
{
    closeDialog();
}

void FileBrowserDialog::buttonClicked(juce::Button* button)
{
    if (button == cancelButton.get())
        cancelButtonClicked();
}