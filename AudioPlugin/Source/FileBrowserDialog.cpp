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

FileBrowserDialog::FileBrowserDialog(const juce::String& title,
                                   const juce::String& filePattern,
                                   const juce::File& initialDirectory,
                                   DialogType dialogType)
    : filePattern(filePattern), dialogType(dialogType)
{
    juce::File startDir = initialDirectory;
    if (dialogType != DialogType::Other)
    {
        juce::File last = getLastDirectory(dialogType);
        if (last.exists() && last.isDirectory())
            startDir = last;
    }
    fileBrowser = std::make_unique<juce::FileBrowserComponent>(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        startDir,
        nullptr,
        nullptr);
    fileBrowser->setSize(600, 400);
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
    try {
        fileSelectedCallback = onFileSelected;
        cancelledCallback = onCancelled;
        // Always set a reasonable default size before showing
        setSize(600, 400);
        dialogWindow = std::make_unique<CustomDialogWindow>("Select File", juce::Colours::darkgrey, true, this);
        dialogWindow->setContentOwned(this, false);
        dialogWindow->setUsingNativeTitleBar(true);
        dialogWindow->setResizable(true, false);
        dialogWindow->setSize(600, 400); // Explicitly set dialog window size
        dialogWindow->setResizeLimits(400, 300, 1920, 1080); // Prevent too small
        dialogWindow->centreAroundComponent(parent, getWidth(), getHeight());
        dialogWindow->setVisible(true);
        addKeyListener(this);
        setWantsKeyboardFocus(true);
        grabKeyboardFocus();
    } catch (const std::exception& e) {
        std::cout << "[FileBrowserDialog] Exception in showDialog: " << e.what() << std::endl;
    } catch (...) {
        std::cout << "[FileBrowserDialog] Unknown exception in showDialog" << std::endl;
    }
}

void FileBrowserDialog::fileDoubleClicked(const juce::File& file)
{
    try {
        if (file.existsAsFile())
        {
            if (dialogType != DialogType::Other)
                setLastDirectory(dialogType, file.getParentDirectory());
            if (fileSelectedCallback) {
                try {
                    fileSelectedCallback(file);
                } catch (const std::exception& e) {
                    std::cout << "[FileBrowserDialog] Exception in fileSelectedCallback (fileDoubleClicked): " << e.what() << std::endl;
                } catch (...) {
                    std::cout << "[FileBrowserDialog] Unknown exception in fileSelectedCallback (fileDoubleClicked)" << std::endl;
                }
            }
            closeDialog();
        }
    } catch (const std::exception& e) {
        std::cout << "[FileBrowserDialog] Exception in fileDoubleClicked: " << e.what() << std::endl;
    } catch (...) {
        std::cout << "[FileBrowserDialog] Unknown exception in fileDoubleClicked" << std::endl;
    }
}

void FileBrowserDialog::fileClicked(const juce::File& file, const juce::MouseEvent&)
{
    try {
        if ((dialogType == DialogType::Voice || dialogType == DialogType::Performance) && file.existsAsFile())
        {
            if (fileSelectedCallback && dialogWindow && dialogWindow->isVisible()) {
                try {
                    fileSelectedCallback(file);
                } catch (const std::exception& e) {
                    std::cout << "[FileBrowserDialog] Exception in fileSelectedCallback (fileClicked): " << e.what() << std::endl;
                } catch (...) {
                    std::cout << "[FileBrowserDialog] Unknown exception in fileSelectedCallback (fileClicked)" << std::endl;
                }
            }
        }
    } catch (const std::exception& e) {
        std::cout << "[FileBrowserDialog] Exception in fileClicked: " << e.what() << std::endl;
    } catch (...) {
        std::cout << "[FileBrowserDialog] Unknown exception in fileClicked" << std::endl;
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

// Static instance for plugin-wide settings
static juce::ApplicationProperties& getAppProperties()
{
    static juce::ApplicationProperties props;
    static bool initialized = false;
    if (!initialized)
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "FMRack";
        options.filenameSuffix = "settings";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName = "FMRack";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        props.setStorageParameters(options);
        initialized = true;
    }
    return props;
}

juce::File FileBrowserDialog::getLastDirectory(DialogType type)
{
    auto* props = getAppProperties().getUserSettings();
    juce::String key = (type == DialogType::Voice) ? "lastVoiceDir" : (type == DialogType::Performance) ? "lastPerformanceDir" : "lastOtherDir";
    juce::String path = props->getValue(key, juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getFullPathName());
    return juce::File(path);
}

void FileBrowserDialog::setLastDirectory(DialogType type, const juce::File& dir)
{
    auto* props = getAppProperties().getUserSettings();
    juce::String key = (type == DialogType::Voice) ? "lastVoiceDir" : (type == DialogType::Performance) ? "lastPerformanceDir" : "lastOtherDir";
    props->setValue(key, dir.getFullPathName());
    props->saveIfNeeded();
}