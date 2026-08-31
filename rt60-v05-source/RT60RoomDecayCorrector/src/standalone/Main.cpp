#include <juce_gui_extra/juce_gui_extra.h>
#include "standalone/MainComponent.hpp"

class RT60Application final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "RT60 Room Decay Corrector"; }
    const juce::String getApplicationVersion() override { return "0.4.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override {
        window_ = std::make_unique<MainWindow>(getApplicationName());
    }
    void shutdown() override { window_.reset(); }
    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow final : public juce::DocumentWindow {
    public:
        explicit MainWindow(juce::String name)
            : DocumentWindow(name, juce::Colour::fromRGB(14,16,20), allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<MainWindow> window_;
};

START_JUCE_APPLICATION(RT60Application)
