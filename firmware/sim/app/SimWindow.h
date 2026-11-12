// Window for one simulated unit: the front panel (3-digit display, status lights,
// TALK / MODE / UP / DOWN) on the left, simulated hardware controls on the right.
#pragma once
#include <QWidget>

class SimUnit;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QSlider;
class DeviceView;

class SimWindow : public QWidget {
    Q_OBJECT
public:
    explicit SimWindow(SimUnit* unit, QWidget* parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;

private:
    void refresh();
    bool keyToButton(int key, int* b) const;

    SimUnit* unit_;
    DeviceView* device_;
    QCheckBox *poe_, *wifi_, *batt_, *aec_, *duck_, *listen_;
    QSlider* battLevel_;
    QComboBox *mic_, *echo_;
    QProgressBar *mMic_, *mSent_, *mSpk_;
    QLabel *status_, *aecInfo_;
    QListWidget* peers_;
    QPlainTextEdit* log_;
};
