#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>

#include <atomic>

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSplitter;
class QToolButton;
class TranslationProvider;
template <typename T>
class QFutureWatcher;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void applyProviderPreset(int index);
    void detectModels();
    void handleModelListReply();
    void startTranslation();
    void requestCancellation();
    void handleProcessingFinished();
    void appendLog(const QString &message);
    void updateProgress(int value);
    void updateStatus(const QString &step, int progress = -1);

private:
    // 启动任务时一次性复制 UI 配置。后台线程只访问该值对象，不触碰控件。
    struct TranslationTask
    {
        QString modPath;
        QString apiKey;
        QString model;
        QString apiUrl;
        QString languageCode;
        QString languageName;
        bool translateAllJson = false;
    };

    struct ProcessingResult
    {
        bool success = false;
        bool cancelled = false;
        int processedFileCount = 0;
        QString errorMessage;
    };

    void setupUi();
    void restoreSettings();
    void saveSettings();
    void saveProviderSettings(const QString &providerId);
    void loadProviderSettings(const QString &providerId);
    void setInputsEnabled(bool enabled);
    void processDroppedPath(const QString &path);
    QString resolvedApiKey() const;
    void updateApiKeyPlaceholder();

    ProcessingResult processModInBackground(const TranslationTask &task);
    bool translateJsonDocument(const QString &sourceFilePath,
                               const QString &targetFilePath,
                               TranslationProvider &provider,
                               const TranslationTask &task,
                               bool reportBatchProgress,
                               QString &errorMessage);

    QComboBox *m_providerCombo = nullptr;
    QLineEdit *m_apiUrlEdit = nullptr;
    QLineEdit *m_apiKeyEdit = nullptr;
    QComboBox *m_modelCombo = nullptr;
    QToolButton *m_detectModelsButton = nullptr;
    QToolButton *m_toggleApiKeyButton = nullptr;
    QComboBox *m_languageCombo = nullptr;
    QCheckBox *m_translateAllJsonCheckBox = nullptr;
    QPushButton *m_browseButton = nullptr;
    QToolButton *m_openModButton = nullptr;
    QLabel *m_dropLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_statusBadge = nullptr;
    QLabel *m_modelHintLabel = nullptr;
    QPlainTextEdit *m_logEdit = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QPushButton *m_startButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QSplitter *m_contentSplitter = nullptr;
    QFutureWatcher<ProcessingResult> *m_futureWatcher = nullptr;
    QNetworkAccessManager *m_modelNetworkManager = nullptr;
    QNetworkReply *m_modelListReply = nullptr;

    QString m_pendingModPath;
    QString m_activeProviderId;
    QString m_apiKeyScopeUrl;
    std::atomic_bool m_cancelRequested{false};
    bool m_restoringSettings = false;
    bool m_isProcessing = false;
    bool m_closeAfterProcessing = false;
};

#endif // MAINWINDOW_H
