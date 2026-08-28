#include "mainwindow.h"

#include "jsonutils.h"
#include "settingssecurity.h"
#include "translationprovider.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCompleter>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStringConverter>
#include <QStyle>
#include <QTextStream>
#include <QThread>
#include <QTime>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <exception>

namespace
{

constexpr int TranslationBatchSize = 30;
constexpr int LanguageNameRole = Qt::UserRole + 1;

// Dynamic properties are used by the stylesheet for small state changes.
// Re-polishing only the changed widget is cheaper than reapplying the whole theme.
void refreshWidgetStyle(QWidget *widget)
{
    if (widget == nullptr)
        return;
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

void loadDotEnvFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    static const QRegularExpression validKey("^[A-Za-z_][A-Za-z0-9_]*$");

    while (!stream.atEnd())
    {
        QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        if (line.startsWith("export "))
            line = line.mid(7).trimmed();

        const qsizetype separator = line.indexOf('=');
        if (separator <= 0)
            continue;

        const QString key = line.left(separator).trimmed();
        QString value = line.mid(separator + 1).trimmed();
        if (!validKey.match(key).hasMatch() || qEnvironmentVariableIsSet(key.toUtf8().constData()))
            continue;

        if (value.size() >= 2
            && ((value.startsWith('"') && value.endsWith('"'))
                || (value.startsWith('\'') && value.endsWith('\''))))
        {
            value = value.mid(1, value.size() - 2);
        }
        qputenv(key.toUtf8().constData(), value.toUtf8());
    }
}

void loadEnvironmentFiles()
{
    QStringList candidates{
        QDir::current().filePath(".env"),
        QDir(QCoreApplication::applicationDirPath()).filePath(".env")};
    candidates.removeDuplicates();
    for (const QString &filePath : candidates)
        loadDotEnvFile(filePath);
}

QString apiKeyFromEnvironment(const QUrl &apiUrl)
{
    const QString host = apiUrl.host().toLower();
    if (host.contains("deepseek"))
    {
        const QString providerKey = qEnvironmentVariable("DEEPSEEK_API_KEY").trimmed();
        if (!providerKey.isEmpty())
            return providerKey;
    }
    if (host.contains("openai"))
    {
        const QString providerKey = qEnvironmentVariable("OPENAI_API_KEY").trimmed();
        if (!providerKey.isEmpty())
            return providerKey;
    }
    if (host.contains("micuapi"))
    {
        const QString providerKey = qEnvironmentVariable("MICUAPI_API_KEY").trimmed();
        if (!providerKey.isEmpty())
            return providerKey;
    }
    return qEnvironmentVariable("STARDEW_TRANSLATOR_API_KEY").trimmed();
}

QString modelListError(const QByteArray &body, const QString &fallback)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    const QString serviceMessage = document.object()
                                       .value("error").toObject()
                                       .value("message").toString().trimmed();
    return serviceMessage.isEmpty() ? fallback : serviceMessage;
}

QString safeResponsePreview(const QByteArray &body)
{
    QString preview = QString::fromUtf8(body.left(512)).simplified();
    preview.replace(QRegularExpression("Bearer\\s+[^\\s<]+",
                                       QRegularExpression::CaseInsensitiveOption),
                    "Bearer ***");
    preview.replace(QRegularExpression("sk-[A-Za-z0-9_-]+"), "sk-***");
    if (preview.size() > 240)
        preview = preview.left(240) + "...";
    return preview.isEmpty() ? QString("<空响应>") : preview;
}

bool looksLikeHtml(const QByteArray &body, const QString &contentType)
{
    QByteArray beginning = body.trimmed().left(64).toLower();
    if (beginning.startsWith(QByteArray::fromHex("efbbbf")))
        beginning.remove(0, 3);
    return contentType.contains("text/html", Qt::CaseInsensitive)
           || beginning.startsWith("<!doctype html")
           || beginning.startsWith("<html");
}

QString jsonParseErrorDescription(const QByteArray &source,
                                  const QJsonParseError &error)
{
    qsizetype offset = static_cast<qsizetype>(error.offset);
    offset = std::clamp(offset, qsizetype{0}, source.size());
    const QString prefix = QString::fromUtf8(source.left(offset));
    const qsizetype line = prefix.count('\n') + 1;
    const qsizetype lastNewline = prefix.lastIndexOf('\n');
    const qsizetype column = prefix.size() - lastNewline;
    return QString("%1，第 %2 行第 %3 列")
        .arg(error.errorString()).arg(line).arg(column);
}

bool isExcludedJsonFile(const QString &rootPath, const QString &filePath)
{
    const QString relativePath = QDir::fromNativeSeparators(
        QDir(rootPath).relativeFilePath(filePath));
    const QStringList parts = relativePath.split('/', Qt::SkipEmptyParts);

    for (qsizetype index = 0; index + 1 < parts.size(); ++index)
    {
        if (parts.at(index).compare("i18n", Qt::CaseInsensitive) == 0)
            return true;
    }

    const QString fileName = QFileInfo(filePath).fileName();
    return fileName.compare("manifest.json", Qt::CaseInsensitive) == 0
           || fileName.compare("config.json", Qt::CaseInsensitive) == 0
           || fileName.compare("config-schema.json", Qt::CaseInsensitive) == 0;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    loadEnvironmentFiles();
    setupUi();
    restoreSettings();
}

MainWindow::~MainWindow()
{
    saveSettings();

    // 正常关闭会先经过 closeEvent。这里仍等待未结束任务，防止后台 lambda
    // 在窗口销毁后访问 this。
    m_cancelRequested.store(true);
    if (m_modelListReply != nullptr)
    {
        m_modelListReply->disconnect(this);
        m_modelListReply->abort();
    }
    if (m_futureWatcher != nullptr && m_futureWatcher->isRunning())
        m_futureWatcher->waitForFinished();
}

void MainWindow::setupUi()
{
    setWindowTitle("StardewTranslator - 星露谷 Mod AI 汉化工具");
    resize(1120, 740);
    setMinimumSize(920, 640);
    setAcceptDrops(true);

    setStyleSheet(R"qss(
        QMainWindow, QWidget#appRoot {
            background: #f3f5f2;
        }
        QWidget {
            color: #25322a;
            font-family: "Microsoft YaHei UI", "Segoe UI";
            font-size: 14px;
            letter-spacing: 0px;
        }
        QLabel#brandMark {
            min-width: 42px;
            max-width: 42px;
            min-height: 42px;
            max-height: 42px;
            color: white;
            background: #3f7150;
            border-radius: 8px;
            font-size: 16px;
            font-weight: 700;
        }
        QLabel#brandTitle {
            color: #17221b;
            font-size: 23px;
            font-weight: 700;
        }
        QLabel#brandSubtitle, QLabel#fieldHint, QLabel#panelEyebrow {
            color: #6c776f;
            font-size: 12px;
        }
        QLabel#sectionTitle, QLabel#panelTitle {
            color: #1d2a22;
            font-size: 16px;
            font-weight: 700;
        }
        QLabel#fieldLabel {
            color: #465149;
            font-size: 13px;
            font-weight: 600;
        }
        QLabel#versionBadge, QLabel#statusBadge {
            min-height: 26px;
            padding: 0 10px;
            border-radius: 7px;
            font-size: 12px;
            font-weight: 600;
        }
        QLabel#versionBadge {
            color: #536158;
            background: #e7ebe7;
        }
        QLabel#statusBadge[state="idle"] {
            color: #536158;
            background: #e7ebe7;
        }
        QLabel#statusBadge[state="working"] {
            color: #2f6240;
            background: #deeee2;
        }
        QLabel#statusBadge[state="success"] {
            color: #27623b;
            background: #dcefe2;
        }
        QLabel#statusBadge[state="warning"] {
            color: #805a19;
            background: #f8eacb;
        }
        QLabel#statusBadge[state="error"] {
            color: #963f43;
            background: #f6dddd;
        }
        QFrame#workspacePanel {
            background: #ffffff;
            border: 1px solid #d9dfda;
            border-radius: 8px;
        }
        QFrame#dropZone {
            background: #f7faf7;
            border: 1px dashed #8ba492;
            border-radius: 8px;
        }
        QFrame#dropZone[selected="true"] {
            background: #edf5ef;
            border: 1px solid #7e9d87;
        }
        QFrame#divider {
            color: #e1e5e1;
            background: #e1e5e1;
            max-height: 1px;
        }
        QScrollArea, QScrollArea > QWidget > QWidget {
            background: transparent;
            border: none;
        }
        QLineEdit, QComboBox {
            min-height: 38px;
            background: #ffffff;
            border: 1px solid #cbd3cc;
            border-radius: 7px;
            padding: 0 10px;
            selection-background-color: #4b7659;
        }
        QLineEdit:hover, QComboBox:hover {
            border-color: #9dab9f;
        }
        QLineEdit:focus, QComboBox:focus {
            border: 2px solid #4b7659;
            padding: 0 9px;
        }
        QLineEdit:disabled, QComboBox:disabled {
            color: #8a948d;
            background: #eef1ee;
        }
        QComboBox::drop-down {
            width: 28px;
            border: none;
        }
        QComboBox QAbstractItemView {
            background: #ffffff;
            border: 1px solid #cbd3cc;
            selection-background-color: #e1ece4;
            selection-color: #25322a;
            outline: none;
        }
        QPushButton, QToolButton#iconButton, QToolButton#revealKeyButton {
            min-height: 36px;
            color: #344138;
            background: #ffffff;
            border: 1px solid #c9d1ca;
            border-radius: 7px;
            padding: 0 12px;
            font-weight: 600;
        }
        QPushButton:hover, QToolButton#iconButton:hover, QToolButton#revealKeyButton:hover {
            background: #edf3ee;
            border-color: #8fa095;
        }
        QPushButton:pressed, QToolButton#iconButton:pressed, QToolButton#revealKeyButton:pressed {
            background: #e0e9e2;
        }
        QPushButton:disabled, QToolButton#iconButton:disabled, QToolButton#revealKeyButton:disabled {
            color: #99a29b;
            background: #eef1ee;
            border-color: #dce1dd;
        }
        QPushButton#primaryButton {
            min-height: 42px;
            color: #ffffff;
            background: #3f7150;
            border: 1px solid #3f7150;
            font-size: 15px;
        }
        QPushButton#primaryButton:hover {
            background: #345f43;
            border-color: #345f43;
        }
        QPushButton#dangerButton {
            min-height: 42px;
            color: #9a3d43;
            background: #fffafa;
            border-color: #e5c3c5;
        }
        QPushButton#quietAction {
            min-height: 29px;
            max-height: 29px;
            padding: 0 9px;
            font-size: 12px;
        }
        QToolButton#iconButton {
            min-width: 38px;
            max-width: 38px;
            min-height: 38px;
            max-height: 38px;
            padding: 0;
        }
        QToolButton#revealKeyButton {
            min-width: 52px;
            max-width: 52px;
            min-height: 38px;
            max-height: 38px;
            padding: 0;
            font-size: 12px;
        }
        QCheckBox {
            spacing: 8px;
            color: #4e5a52;
        }
        QProgressBar {
            min-height: 12px;
            max-height: 12px;
            background: #e5e9e5;
            border: none;
            border-radius: 6px;
            text-align: center;
        }
        QProgressBar::chunk {
            background: #4b7659;
            border-radius: 6px;
        }
        QPlainTextEdit#logView {
            color: #2c3931;
            background: #f8faf8;
            border: 1px solid #dce2dd;
            border-radius: 7px;
            padding: 10px;
            selection-background-color: #d8e8dc;
        }
        QSplitter::handle {
            background: transparent;
            width: 16px;
        }
        QScrollBar:vertical {
            width: 10px;
            background: transparent;
            margin: 2px;
        }
        QScrollBar::handle:vertical {
            min-height: 30px;
            background: #c7cec8;
            border-radius: 4px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }
    )qss");

    auto *centralWidget = new QWidget(this);
    centralWidget->setObjectName("appRoot");
    setCentralWidget(centralWidget);
    auto *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(24, 20, 24, 24);
    mainLayout->setSpacing(18);

    auto *headerLayout = new QHBoxLayout;
    headerLayout->setSpacing(12);
    auto *brandMark = new QLabel("ST", centralWidget);
    brandMark->setObjectName("brandMark");
    brandMark->setAlignment(Qt::AlignCenter);
    auto *brandTextLayout = new QVBoxLayout;
    brandTextLayout->setContentsMargins(0, 0, 0, 0);
    brandTextLayout->setSpacing(1);
    auto *brandTitle = new QLabel("StardewTranslator", centralWidget);
    brandTitle->setObjectName("brandTitle");
    auto *brandSubtitle = new QLabel("星露谷 Mod AI 汉化工具", centralWidget);
    brandSubtitle->setObjectName("brandSubtitle");
    brandTextLayout->addWidget(brandTitle);
    brandTextLayout->addWidget(brandSubtitle);
    auto *versionBadge = new QLabel("本地工具  v1.0", centralWidget);
    versionBadge->setObjectName("versionBadge");
    versionBadge->setAlignment(Qt::AlignCenter);
    headerLayout->addWidget(brandMark);
    headerLayout->addLayout(brandTextLayout);
    headerLayout->addStretch();
    headerLayout->addWidget(versionBadge);
    mainLayout->addLayout(headerLayout);

    m_contentSplitter = new QSplitter(Qt::Horizontal, centralWidget);
    m_contentSplitter->setChildrenCollapsible(false);
    m_contentSplitter->setHandleWidth(16);

    auto *settingsPanel = new QWidget(m_contentSplitter);
    settingsPanel->setMinimumWidth(320);
    auto *settingsPanelLayout = new QVBoxLayout(settingsPanel);
    settingsPanelLayout->setContentsMargins(0, 0, 4, 0);
    settingsPanelLayout->setSpacing(12);
    auto *settingsScroll = new QScrollArea(settingsPanel);
    settingsScroll->setWidgetResizable(true);
    settingsScroll->setFrameShape(QFrame::NoFrame);
    settingsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *settingsContent = new QWidget(settingsScroll);
    auto *settingsLayout = new QVBoxLayout(settingsContent);
    settingsLayout->setContentsMargins(0, 0, 6, 0);
    settingsLayout->setSpacing(13);

    auto makeSectionTitle = [settingsContent](const QString &text) {
        auto *label = new QLabel(text, settingsContent);
        label->setObjectName("sectionTitle");
        return label;
    };
    auto makeFieldLabel = [settingsContent](const QString &text) {
        auto *label = new QLabel(text, settingsContent);
        label->setObjectName("fieldLabel");
        return label;
    };
    auto makeDivider = [settingsContent]() {
        auto *divider = new QFrame(settingsContent);
        divider->setObjectName("divider");
        divider->setFrameShape(QFrame::HLine);
        return divider;
    };

    settingsLayout->addWidget(makeSectionTitle("1  选择 Mod"));
    auto *dropZone = new QFrame(settingsContent);
    dropZone->setObjectName("dropZone");
    auto *dropLayout = new QVBoxLayout(dropZone);
    dropLayout->setContentsMargins(16, 14, 16, 14);
    dropLayout->setSpacing(10);
    m_dropLabel = new QLabel("尚未选择 Mod 文件夹", dropZone);
    m_dropLabel->setAlignment(Qt::AlignCenter);
    m_dropLabel->setMinimumHeight(48);
    m_dropLabel->setWordWrap(true);
    m_dropLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *modButtonLayout = new QHBoxLayout;
    modButtonLayout->setSpacing(8);
    m_browseButton = new QPushButton("选择文件夹", dropZone);
    m_browseButton->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    m_openModButton = new QToolButton(dropZone);
    m_openModButton->setObjectName("iconButton");
    m_openModButton->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
    m_openModButton->setToolTip("在资源管理器中打开 Mod 文件夹");
    m_openModButton->setAccessibleName("打开 Mod 文件夹");
    m_openModButton->setEnabled(false);
    modButtonLayout->addWidget(m_browseButton, 1);
    modButtonLayout->addWidget(m_openModButton);
    dropLayout->addWidget(m_dropLabel);
    dropLayout->addLayout(modButtonLayout);
    settingsLayout->addWidget(dropZone);
    settingsLayout->addWidget(makeDivider());

    settingsLayout->addWidget(makeSectionTitle("2  AI 接口"));
    settingsLayout->addWidget(makeFieldLabel("服务商"));
    m_providerCombo = new QComboBox(settingsContent);
    m_providerCombo->addItem("DeepSeek（推荐）", "deepseek");
    m_providerCombo->addItem("MicuAPI（国内兼容）", "micuapi");
    m_providerCombo->addItem("OpenAI", "openai");
    m_providerCombo->addItem("自定义兼容接口", "custom");
    settingsLayout->addWidget(m_providerCombo);

    settingsLayout->addWidget(makeFieldLabel("接口地址"));
    m_apiUrlEdit = new QLineEdit(settingsContent);
    m_apiUrlEdit->setClearButtonEnabled(true);
    settingsLayout->addWidget(m_apiUrlEdit);

    settingsLayout->addWidget(makeFieldLabel("API Key"));
    auto *apiKeyLayout = new QHBoxLayout;
    apiKeyLayout->setSpacing(8);
    m_apiKeyEdit = new QLineEdit(settingsContent);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setClearButtonEnabled(true);
    m_toggleApiKeyButton = new QToolButton(settingsContent);
    m_toggleApiKeyButton->setObjectName("revealKeyButton");
    m_toggleApiKeyButton->setText("显示");
    m_toggleApiKeyButton->setCheckable(true);
    m_toggleApiKeyButton->setToolTip("显示或隐藏 API Key");
    apiKeyLayout->addWidget(m_apiKeyEdit, 1);
    apiKeyLayout->addWidget(m_toggleApiKeyButton);
    settingsLayout->addLayout(apiKeyLayout);

    settingsLayout->addWidget(makeFieldLabel("模型"));
    auto *modelLayout = new QHBoxLayout;
    modelLayout->setSpacing(8);
    m_modelCombo = new QComboBox(settingsContent);
    m_modelCombo->setEditable(true);
    m_modelCombo->setInsertPolicy(QComboBox::NoInsert);
    m_modelCombo->setPlaceholderText("输入模型名称");
    m_modelCombo->completer()->setFilterMode(Qt::MatchContains);
    m_modelCombo->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    m_detectModelsButton = new QToolButton(settingsContent);
    m_detectModelsButton->setObjectName("iconButton");
    m_detectModelsButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    m_detectModelsButton->setToolTip("检测接口可用模型");
    m_detectModelsButton->setAccessibleName("检测可用模型");
    modelLayout->addWidget(m_modelCombo, 1);
    modelLayout->addWidget(m_detectModelsButton);
    settingsLayout->addLayout(modelLayout);
    m_modelHintLabel = new QLabel("等待检测", settingsContent);
    m_modelHintLabel->setObjectName("fieldHint");
    settingsLayout->addWidget(m_modelHintLabel);
    settingsLayout->addWidget(makeDivider());

    settingsLayout->addWidget(makeSectionTitle("3  输出"));
    settingsLayout->addWidget(makeFieldLabel("目标语言"));
    m_languageCombo = new QComboBox(settingsContent);
    const auto addLanguage = [this](const QString &label,
                                    const QString &code,
                                    const QString &promptName) {
        m_languageCombo->addItem(label, code);
        m_languageCombo->setItemData(m_languageCombo->count() - 1,
                                     promptName,
                                     LanguageNameRole);
    };
    addLanguage("简体中文", "zh", "Simplified Chinese");
    addLanguage("英语", "en", "English");
    addLanguage("日语", "ja", "Japanese");
    addLanguage("韩语", "ko", "Korean");
    settingsLayout->addWidget(m_languageCombo);
    m_translateAllJsonCheckBox = new QCheckBox(
        "无 i18n 时翻译全部 JSON（实验性）", settingsContent);
    settingsLayout->addWidget(m_translateAllJsonCheckBox);
    settingsLayout->addStretch();
    settingsScroll->setWidget(settingsContent);
    settingsPanelLayout->addWidget(settingsScroll, 1);

    auto *translationButtons = new QHBoxLayout;
    translationButtons->setSpacing(8);
    m_startButton = new QPushButton("开始翻译", settingsPanel);
    m_startButton->setObjectName("primaryButton");
    m_startButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_startButton->setEnabled(false);
    m_cancelButton = new QPushButton("取消", settingsPanel);
    m_cancelButton->setObjectName("dangerButton");
    m_cancelButton->setIcon(style()->standardIcon(QStyle::SP_DialogCancelButton));
    m_cancelButton->setEnabled(false);
    translationButtons->addWidget(m_startButton, 1);
    translationButtons->addWidget(m_cancelButton);
    settingsPanelLayout->addLayout(translationButtons);

    auto *taskPanel = new QFrame(m_contentSplitter);
    taskPanel->setObjectName("workspacePanel");
    taskPanel->setMinimumWidth(430);
    auto *taskLayout = new QVBoxLayout(taskPanel);
    taskLayout->setContentsMargins(22, 20, 22, 20);
    taskLayout->setSpacing(12);
    auto *taskTitleLayout = new QHBoxLayout;
    auto *taskTitles = new QVBoxLayout;
    taskTitles->setSpacing(2);
    auto *taskEyebrow = new QLabel("TRANSLATION TASK", taskPanel);
    taskEyebrow->setObjectName("panelEyebrow");
    auto *taskTitle = new QLabel("当前任务", taskPanel);
    taskTitle->setObjectName("panelTitle");
    taskTitles->addWidget(taskEyebrow);
    taskTitles->addWidget(taskTitle);
    m_statusBadge = new QLabel("等待中", taskPanel);
    m_statusBadge->setObjectName("statusBadge");
    m_statusBadge->setProperty("state", "idle");
    m_statusBadge->setAlignment(Qt::AlignCenter);
    taskTitleLayout->addLayout(taskTitles);
    taskTitleLayout->addStretch();
    taskTitleLayout->addWidget(m_statusBadge, 0, Qt::AlignTop);
    taskLayout->addLayout(taskTitleLayout);

    m_statusLabel = new QLabel("等待选择 Mod", taskPanel);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setMinimumHeight(38);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    taskLayout->addWidget(m_statusLabel);
    m_progressBar = new QProgressBar(taskPanel);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    taskLayout->addWidget(m_progressBar);
    taskLayout->addSpacing(8);

    auto *logTitleLayout = new QHBoxLayout;
    auto *logTitle = new QLabel("运行日志", taskPanel);
    logTitle->setObjectName("sectionTitle");
    auto *copyLogButton = new QPushButton("复制", taskPanel);
    copyLogButton->setObjectName("quietAction");
    copyLogButton->setToolTip("复制全部日志");
    auto *clearLogButton = new QPushButton("清空", taskPanel);
    clearLogButton->setObjectName("quietAction");
    clearLogButton->setToolTip("清空日志");
    logTitleLayout->addWidget(logTitle);
    logTitleLayout->addStretch();
    logTitleLayout->addWidget(copyLogButton);
    logTitleLayout->addWidget(clearLogButton);
    taskLayout->addLayout(logTitleLayout);

    m_logEdit = new QPlainTextEdit(taskPanel);
    m_logEdit->setObjectName("logView");
    m_logEdit->setReadOnly(true);
    m_logEdit->setMaximumBlockCount(2000);
    m_logEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_logEdit->setPlaceholderText("任务日志将在这里显示");
    taskLayout->addWidget(m_logEdit, 1);

    m_contentSplitter->addWidget(settingsPanel);
    m_contentSplitter->addWidget(taskPanel);
    m_contentSplitter->setStretchFactor(0, 0);
    m_contentSplitter->setStretchFactor(1, 1);
    m_contentSplitter->setSizes({380, 700});
    mainLayout->addWidget(m_contentSplitter, 1);

    m_futureWatcher = new QFutureWatcher<ProcessingResult>(this);
    m_modelNetworkManager = new QNetworkAccessManager(this);
    connect(m_providerCombo,
            &QComboBox::currentIndexChanged,
            this,
            &MainWindow::applyProviderPreset);
    connect(m_apiUrlEdit,
            &QLineEdit::textChanged,
            this,
            [this]() { updateApiKeyPlaceholder(); });
    connect(m_apiUrlEdit,
            &QLineEdit::textEdited,
            this,
            [this](const QString &editedUrl) {
                const int customIndex = m_providerCombo->findData("custom");
                if (customIndex >= 0 && m_providerCombo->currentIndex() != customIndex)
                    m_providerCombo->setCurrentIndex(customIndex);

                const QString scopedHost = QUrl(m_apiKeyScopeUrl).host();
                const QString editedHost = QUrl(editedUrl).host();
                if (!m_apiKeyEdit->text().isEmpty()
                    && !scopedHost.isEmpty()
                    && scopedHost.compare(editedHost, Qt::CaseInsensitive) != 0)
                {
                    m_apiKeyEdit->clear();
                    m_apiKeyScopeUrl.clear();
                    appendLog("接口域名已改变，已清除当前 API Key。请填写新接口对应的 Key。");
                }
                m_apiUrlEdit->setText(editedUrl);
            });
    connect(m_apiKeyEdit,
            &QLineEdit::textEdited,
            this,
            [this]() { m_apiKeyScopeUrl = m_apiUrlEdit->text().trimmed(); });
    connect(m_toggleApiKeyButton,
            &QToolButton::toggled,
            this,
            [this](bool visible) {
                m_apiKeyEdit->setEchoMode(visible ? QLineEdit::Normal
                                                  : QLineEdit::Password);
                m_toggleApiKeyButton->setText(visible ? "隐藏" : "显示");
            });
    connect(m_browseButton,
            &QPushButton::clicked,
            this,
            [this]() {
                const QString initialPath = m_pendingModPath.isEmpty()
                                                ? QDir::homePath()
                                                : m_pendingModPath;
                const QString directory = QFileDialog::getExistingDirectory(
                    this, "选择已解压的 Mod 文件夹", initialPath,
                    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
                if (!directory.isEmpty())
                    processDroppedPath(directory);
            });
    connect(m_openModButton,
            &QToolButton::clicked,
            this,
            [this]() {
                if (!m_pendingModPath.isEmpty())
                    QDesktopServices::openUrl(QUrl::fromLocalFile(m_pendingModPath));
            });
    connect(m_detectModelsButton,
            &QToolButton::clicked,
            this,
            &MainWindow::detectModels);
    connect(copyLogButton,
            &QPushButton::clicked,
            this,
            [this]() {
                QApplication::clipboard()->setText(m_logEdit->toPlainText());
            });
    connect(clearLogButton,
            &QPushButton::clicked,
            m_logEdit,
            &QPlainTextEdit::clear);
    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::startTranslation);
    connect(m_cancelButton, &QPushButton::clicked, this, &MainWindow::requestCancellation);
    connect(m_futureWatcher,
            &QFutureWatcher<ProcessingResult>::finished,
            this,
            &MainWindow::handleProcessingFinished);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (m_isProcessing || !event->mimeData()->hasUrls())
        return;

    const QList<QUrl> urls = event->mimeData()->urls();
    if (!urls.isEmpty() && QFileInfo(urls.first().toLocalFile()).isDir())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (m_isProcessing || !event->mimeData()->hasUrls())
        return;

    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty())
        return;

    processDroppedPath(urls.first().toLocalFile());
    event->acceptProposedAction();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!m_isProcessing)
    {
        saveSettings();
        event->accept();
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        "翻译仍在进行",
        "是否取消任务并在当前网络请求结束后退出？",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer == QMessageBox::Yes)
    {
        m_closeAfterProcessing = true;
        requestCancellation();
    }
    event->ignore();
}

void MainWindow::processDroppedPath(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir())
    {
        QMessageBox::warning(this, "无效路径", "请选择已解压的 Mod 文件夹。");
        updateStatus("所选路径无效");
        return;
    }

    m_pendingModPath = info.absoluteFilePath();
    m_dropLabel->setText(QString("%1\n%2")
                             .arg(info.fileName(),
                                  QDir::toNativeSeparators(m_pendingModPath)));
    m_dropLabel->setToolTip(QDir::toNativeSeparators(m_pendingModPath));
    if (QWidget *dropZone = m_dropLabel->parentWidget())
    {
        dropZone->setProperty("selected", true);
        refreshWidgetStyle(dropZone);
    }
    m_openModButton->setEnabled(true);
    m_startButton->setEnabled(true);
    updateStatus("准备就绪：" + info.fileName(), 0);
    appendLog("已加载 Mod: " + QDir::toNativeSeparators(m_pendingModPath));
}

void MainWindow::restoreSettings()
{
    QSettings settings;
    const bool hasSavedSettings = settings.contains("selection/provider");

    const QByteArray geometry = settings.value("window/geometry").toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
    const QByteArray splitterState = settings.value("window/splitter").toByteArray();
    if (!splitterState.isEmpty())
        m_contentSplitter->restoreState(splitterState);

    m_restoringSettings = true;
    const QString providerId = settings.value("selection/provider", "deepseek").toString();
    int providerIndex = m_providerCombo->findData(providerId);
    if (providerIndex < 0)
        providerIndex = m_providerCombo->findData("deepseek");
    {
        const QSignalBlocker blocker(m_providerCombo);
        m_providerCombo->setCurrentIndex(providerIndex);
    }
    applyProviderPreset(providerIndex);

    const QString languageCode = settings.value("selection/language", "zh").toString();
    const int languageIndex = m_languageCombo->findData(languageCode);
    if (languageIndex >= 0)
        m_languageCombo->setCurrentIndex(languageIndex);
    m_translateAllJsonCheckBox->setChecked(
        settings.value("selection/translateAllJson", false).toBool());
    m_restoringSettings = false;

    const QString lastModPath = settings.value("selection/lastModPath").toString();
    if (!lastModPath.isEmpty() && QFileInfo(lastModPath).isDir())
        processDroppedPath(lastModPath);
    else
        updateStatus("等待选择 Mod", 0);

    appendLog(hasSavedSettings ? "已恢复上次使用的设置。" : "等待 Mod 文件夹。");
}

void MainWindow::saveSettings()
{
    if (m_providerCombo == nullptr || m_activeProviderId.isEmpty())
        return;

    saveProviderSettings(m_activeProviderId);
    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/splitter", m_contentSplitter->saveState());
    settings.setValue("selection/provider", m_activeProviderId);
    settings.setValue("selection/language", m_languageCombo->currentData().toString());
    settings.setValue("selection/translateAllJson",
                      m_translateAllJsonCheckBox->isChecked());
    if (m_pendingModPath.isEmpty())
        settings.remove("selection/lastModPath");
    else
        settings.setValue("selection/lastModPath", m_pendingModPath);
    settings.sync();
    if (settings.status() != QSettings::NoError && m_logEdit != nullptr)
        appendLog("警告：无法保存本地设置。");
}

void MainWindow::saveProviderSettings(const QString &providerId)
{
    if (providerId.isEmpty())
        return;

    QSettings settings;
    settings.beginGroup("providers/" + providerId);
    settings.setValue("model", m_modelCombo->currentText().trimmed());
    if (providerId == "custom")
        settings.setValue("apiUrl", m_apiUrlEdit->text().trimmed());

    const QString apiKey = TranslationProvider::normalizeApiKey(m_apiKeyEdit->text());
    if (apiKey.isEmpty())
    {
        settings.remove("apiKey");
        settings.remove("apiKeyUrl");
    }
    else
    {
        QString encryptionError;
        const QByteArray protectedKey =
            SettingsSecurity::protectSecret(apiKey, &encryptionError);
        if (!protectedKey.isEmpty())
        {
            settings.setValue("apiKey", protectedKey);
            settings.setValue("apiKeyUrl", m_apiUrlEdit->text().trimmed());
        }
        else
        {
            settings.remove("apiKey");
            settings.remove("apiKeyUrl");
            if (!encryptionError.isEmpty() && m_logEdit != nullptr)
                appendLog("无法安全保存 API Key：" + encryptionError);
        }
    }
    settings.endGroup();
}

void MainWindow::loadProviderSettings(const QString &providerId)
{
    QSettings settings;
    settings.beginGroup("providers/" + providerId);

    if (providerId == "custom")
        m_apiUrlEdit->setText(settings.value("apiUrl").toString().trimmed());

    const QString savedModel = settings.value("model").toString().trimmed();
    if (!savedModel.isEmpty())
        m_modelCombo->setCurrentText(savedModel);

    const QByteArray protectedKey = settings.value("apiKey").toByteArray();
    const QString keyUrl = settings.value("apiKeyUrl").toString().trimmed();
    const bool keyMatchesEndpoint = providerId != "custom"
                                    || keyUrl == m_apiUrlEdit->text().trimmed();
    QString apiKey;
    if (!protectedKey.isEmpty() && keyMatchesEndpoint)
    {
        QString decryptionError;
        apiKey = SettingsSecurity::unprotectSecret(protectedKey, &decryptionError);
        if (!decryptionError.isEmpty())
        {
            settings.remove("apiKey");
            settings.remove("apiKeyUrl");
            appendLog("无法读取已保存的 API Key，已清除损坏的本地密文。");
        }
    }
    settings.endGroup();

    m_apiKeyEdit->setText(apiKey);
    m_apiKeyScopeUrl = apiKey.isEmpty() ? QString() : m_apiUrlEdit->text().trimmed();
}

void MainWindow::applyProviderPreset(int index)
{
    Q_UNUSED(index)
    const QString providerId = m_providerCombo->currentData().toString();

    // Switching services must never leave a previously entered secret visible.
    m_toggleApiKeyButton->setChecked(false);

    if (!m_restoringSettings
        && !m_activeProviderId.isEmpty()
        && m_activeProviderId != providerId)
        saveProviderSettings(m_activeProviderId);

    m_activeProviderId = providerId;
    m_apiKeyEdit->clear();
    m_apiKeyScopeUrl.clear();

    if (providerId == "deepseek")
    {
        m_apiUrlEdit->setText("https://api.deepseek.com/chat/completions");
        m_modelCombo->clear();
        m_modelCombo->addItems({"deepseek-chat", "deepseek-reasoner"});
    }
    else if (providerId == "openai")
    {
        m_apiUrlEdit->setText("https://api.openai.com/v1/chat/completions");
        m_modelCombo->clear();
        m_modelCombo->addItem("gpt-4o-mini");
    }
    else if (providerId == "micuapi")
    {
        m_apiUrlEdit->setText("https://www.micuapi.ai/v1/chat/completions");
        m_modelCombo->clear();
    }
    else
    {
        m_apiUrlEdit->clear();
        m_modelCombo->clear();
    }

    loadProviderSettings(providerId);
    m_modelHintLabel->setText("等待检测");
    if (!m_restoringSettings)
        appendLog("已切换服务商，并恢复该服务商上次保存的设置。");

    updateApiKeyPlaceholder();
}

QString MainWindow::resolvedApiKey() const
{
    const QString enteredKey =
        TranslationProvider::normalizeApiKey(m_apiKeyEdit->text());
    if (!enteredKey.isEmpty())
        return enteredKey;
    return TranslationProvider::normalizeApiKey(
        apiKeyFromEnvironment(QUrl(m_apiUrlEdit->text().trimmed())));
}

void MainWindow::updateApiKeyPlaceholder()
{
    if (!m_apiKeyEdit->text().isEmpty())
        return;

    const bool hasEnvironmentKey =
        !apiKeyFromEnvironment(QUrl(m_apiUrlEdit->text().trimmed())).isEmpty();
    m_apiKeyEdit->setPlaceholderText(
        hasEnvironmentKey
            ? "已从环境变量或 .env 读取"
            : (SettingsSecurity::isAvailable()
                   ? "输入后将为当前 Windows 用户加密保存"
                   : "可手动输入；建议通过环境变量提供"));
}

void MainWindow::detectModels()
{
    if (m_modelListReply != nullptr)
        return;

    const QString enteredUrl = m_apiUrlEdit->text().trimmed();
    QString endpointError;
    const QUrl apiUrl = TranslationProvider::resolveChatCompletionsUrl(
        QUrl(enteredUrl), &endpointError);
    if (!endpointError.isEmpty())
    {
        QMessageBox::warning(this, "接口地址无效", endpointError);
        return;
    }
    if (apiUrl.toString() != enteredUrl)
    {
        m_apiUrlEdit->setText(apiUrl.toString());
        appendLog("已补全接口地址: " + apiUrl.toString());
    }

    const QUrl modelsUrl = TranslationProvider::modelListUrl(apiUrl);
    QNetworkRequest request(modelsUrl);
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    const QString apiKey = resolvedApiKey();
    if (!apiKey.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());
    request.setTransferTimeout(20000);

    saveSettings();
    updateStatus("正在检测接口可用模型...");
    m_modelHintLabel->setText("正在检测模型...");
    m_detectModelsButton->setEnabled(false);
    m_providerCombo->setEnabled(false);
    m_apiUrlEdit->setEnabled(false);
    m_apiKeyEdit->setEnabled(false);
    m_toggleApiKeyButton->setEnabled(false);
    m_modelCombo->setEnabled(false);
    m_startButton->setEnabled(false);
    appendLog("正在检测模型: " + modelsUrl.toString());
    m_modelListReply = m_modelNetworkManager->get(request);
    connect(m_modelListReply,
            &QNetworkReply::finished,
            this,
            &MainWindow::handleModelListReply);
}

void MainWindow::handleModelListReply()
{
    QNetworkReply *reply = m_modelListReply;
    if (reply == nullptr)
        return;

    m_modelListReply = nullptr;
    m_detectModelsButton->setEnabled(!m_isProcessing);
    m_providerCombo->setEnabled(!m_isProcessing);
    m_apiUrlEdit->setEnabled(!m_isProcessing);
    m_apiKeyEdit->setEnabled(!m_isProcessing);
    m_toggleApiKeyButton->setEnabled(!m_isProcessing);
    m_modelCombo->setEnabled(!m_isProcessing);
    m_startButton->setEnabled(!m_isProcessing && !m_pendingModPath.isEmpty());

    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const QString contentType = QString::fromLatin1(reply->rawHeader("Content-Type"));
    const QUrl finalUrl = reply->url();
    reply->deleteLater();

    if (networkError != QNetworkReply::NoError || statusCode < 200 || statusCode >= 300)
    {
        const QString detail = modelListError(body, networkErrorText);
        const QString error = statusCode > 0
                                  ? QString("模型检测失败（HTTP %1）：%2").arg(statusCode).arg(detail)
                                  : QString("模型检测失败：%1").arg(detail);
        const QString providerHint = (statusCode == 401 || statusCode == 403)
                                         ? QString("\n当前接口：%1\n请确认 Key 由该服务商签发。")
                                               .arg(finalUrl.host())
                                         : QString();
        appendLog(error);
        updateStatus("模型检测失败，请查看日志");
        m_modelHintLabel->setText("检测失败");
        QMessageBox::warning(this,
                             "无法检测模型",
                             error + providerHint + "\n仍可手动输入模型名称。");
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        const QString reason = looksLikeHtml(body, contentType)
                                   ? QString("模型接口返回了网页 HTML。当前地址不是 OpenAI 兼容 API，"
                                             "请使用服务商预设或填写正确的 /v1 Base URL。")
                                   : QString("模型列表不是有效 JSON: %1").arg(parseError.errorString());
        const QString error = QString("%1\n接口：%2\nContent-Type：%3\n响应摘要：%4")
                                  .arg(reason,
                                       finalUrl.toString(),
                                       contentType.isEmpty() ? QString("未知") : contentType,
                                       safeResponsePreview(body));
        appendLog(error);
        updateStatus("模型检测失败，请查看日志");
        m_modelHintLabel->setText("响应格式无效");
        QMessageBox::warning(this, "无法检测模型", error);
        return;
    }

    const QJsonObject root = document.object();
    QJsonArray modelItems = root.value("data").toArray();
    if (modelItems.isEmpty())
        modelItems = root.value("models").toArray();

    QStringList models;
    for (const QJsonValue &item : modelItems)
    {
        QString modelId;
        if (item.isString())
            modelId = item.toString().trimmed();
        else if (item.isObject())
        {
            const QJsonObject object = item.toObject();
            modelId = object.value("id").toString().trimmed();
            if (modelId.isEmpty())
                modelId = object.value("name").toString().trimmed();
        }
        if (!modelId.isEmpty())
            models.append(modelId);
    }

    models.removeDuplicates();
    std::sort(models.begin(), models.end(), [](const QString &left, const QString &right) {
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    if (models.isEmpty())
    {
        const QString error = "接口返回成功，但没有可识别的模型名称。";
        appendLog(error);
        updateStatus("接口未返回可用模型");
        m_modelHintLabel->setText("未检测到模型");
        QMessageBox::warning(this, "未检测到模型", error);
        return;
    }

    const QString previousModel = m_modelCombo->currentText().trimmed();
    m_modelCombo->clear();
    m_modelCombo->addItems(models);
    const int previousIndex = m_modelCombo->findText(previousModel);
    m_modelCombo->setCurrentIndex(previousIndex >= 0 ? previousIndex : 0);
    saveSettings();
    updateStatus(QString("模型检测完成：%1 个可用模型").arg(models.size()));
    m_modelHintLabel->setText(QString("已检测到 %1 个可用模型").arg(models.size()));
    appendLog(QString("已检测到 %1 个模型。").arg(models.size()));
}

void MainWindow::startTranslation()
{
    if (m_pendingModPath.isEmpty() || m_isProcessing)
        return;

    const QString model = m_modelCombo->currentText().trimmed();
    if (model.isEmpty())
    {
        QMessageBox::warning(this, "缺少模型", "请输入接口支持的模型名称。");
        return;
    }

    const QString enteredUrl = m_apiUrlEdit->text().trimmed();
    QString endpointError;
    const QUrl apiUrl = TranslationProvider::resolveChatCompletionsUrl(
        QUrl(enteredUrl), &endpointError);
    if (!endpointError.isEmpty())
    {
        QMessageBox::warning(this, "接口地址无效", endpointError);
        return;
    }
    if (apiUrl.toString() != enteredUrl)
    {
        m_apiUrlEdit->setText(apiUrl.toString());
        appendLog("已补全接口地址: " + apiUrl.toString());
    }

    TranslationTask task;
    task.modPath = m_pendingModPath;
    task.apiKey = resolvedApiKey();
    if (task.apiKey.isEmpty() && m_providerCombo->currentData().toString() != "custom")
    {
        QMessageBox::warning(
            this,
            "缺少 API Key",
            "请手动输入 API Key，或在环境变量/.env 中配置对应服务商的 Key。");
        return;
    }
    task.model = model;
    task.apiUrl = apiUrl.toString();
    task.languageCode = m_languageCombo->currentData().toString();
    task.languageName = m_languageCombo->currentData(LanguageNameRole).toString();
    task.translateAllJson = m_translateAllJsonCheckBox->isChecked();
    saveSettings();

    appendLog(QString("使用接口 %1，模型 %2。")
                  .arg(apiUrl.host(), task.model));

    m_isProcessing = true;
    m_closeAfterProcessing = false;
    m_cancelRequested.store(false);
    m_progressBar->setValue(0);
    setInputsEnabled(false);
    updateStatus("正在扫描 Mod 文件结构...", 0);
    appendLog("开始扫描 Mod...");

    m_futureWatcher->setFuture(QtConcurrent::run([this, task]() {
        return processModInBackground(task);
    }));
}

void MainWindow::requestCancellation()
{
    if (!m_isProcessing || m_cancelRequested.exchange(true))
        return;

    m_cancelButton->setEnabled(false);
    updateStatus("正在取消，将等待当前网络请求结束...");
    appendLog("已请求取消，将在当前网络请求结束后停止。");
}

void MainWindow::handleProcessingFinished()
{
    ProcessingResult result;
    try
    {
        result = m_futureWatcher->result();
    }
    catch (const std::exception &exception)
    {
        result.errorMessage = QString::fromUtf8(exception.what());
    }
    catch (...)
    {
        result.errorMessage = "后台任务发生未知异常";
    }

    m_isProcessing = false;
    setInputsEnabled(true);

    if (result.cancelled)
    {
        updateStatus("任务已取消");
        appendLog("任务已取消，未完成的文件不会被写入。");
    }
    else if (!result.success)
    {
        updateStatus("处理失败，请查看日志");
        appendLog("处理失败: " + result.errorMessage);
        QMessageBox::critical(this, "翻译失败", result.errorMessage);
    }
    else
    {
        m_progressBar->setValue(100);
        updateStatus(QString("处理完成：已写入 %1 个文件").arg(result.processedFileCount),
                     100);
        appendLog(QString("处理完成，共写入 %1 个文件。").arg(result.processedFileCount));
    }

    if (m_closeAfterProcessing)
        close();
}

void MainWindow::setInputsEnabled(bool enabled)
{
    if (!enabled)
        m_toggleApiKeyButton->setChecked(false);
    m_browseButton->setEnabled(enabled);
    m_openModButton->setEnabled(enabled && !m_pendingModPath.isEmpty());
    m_providerCombo->setEnabled(enabled);
    m_apiUrlEdit->setEnabled(enabled);
    m_apiKeyEdit->setEnabled(enabled);
    m_toggleApiKeyButton->setEnabled(enabled);
    m_modelCombo->setEnabled(enabled);
    m_detectModelsButton->setEnabled(enabled && m_modelListReply == nullptr);
    m_languageCombo->setEnabled(enabled);
    m_translateAllJsonCheckBox->setEnabled(enabled);
    m_startButton->setEnabled(enabled && !m_pendingModPath.isEmpty());
    m_cancelButton->setEnabled(!enabled);
}

MainWindow::ProcessingResult MainWindow::processModInBackground(const TranslationTask &task)
{
    ProcessingResult result;
    updateStatus("正在检查 Mod 文件夹...", 1);
    const QFileInfo modInfo(task.modPath);
    if (!modInfo.exists() || !modInfo.isDir())
    {
        result.errorMessage = "Mod 文件夹不存在或无法访问";
        return result;
    }

    TranslationProvider provider(
        task.apiKey,
        task.model,
        task.apiUrl,
        [this](const QString &message) {
            updateStatus(message);
            appendLog(message);
        });
    const QString i18nDirectory = QDir(task.modPath).filePath("i18n");
    const QString defaultFile = QDir(i18nDirectory).filePath("default.json");

    // 标准 SMAPI i18n Mod：只读取 default.json，并生成对应语言文件。
    if (QFileInfo::exists(defaultFile))
    {
        updateStatus("已找到 i18n/default.json，准备读取...", 3);
        appendLog(QString("检测到 i18n/default.json，将生成 %1.json").arg(task.languageCode));
        const QString targetFile = QDir(i18nDirectory).filePath(task.languageCode + ".json");
        if (!translateJsonDocument(defaultFile,
                                   targetFile,
                                   provider,
                                   task,
                                   true,
                                   result.errorMessage))
        {
            result.cancelled = m_cancelRequested.load();
            return result;
        }

        result.success = true;
        result.processedFileCount = 1;
        return result;
    }

    // 任意 JSON 中同时存在显示文本和机器字段。默认拒绝全量翻译，只有用户
    // 明确启用实验选项时才继续，并始终从原始备份重新生成结果。
    if (!task.translateAllJson)
    {
        result.errorMessage =
            "未找到 i18n/default.json。若确认该 Mod 的 JSON 字符串都可翻译，"
            "请启用“翻译全部 JSON（实验性）”。";
        return result;
    }

    QStringList jsonFiles;
    updateStatus("正在查找可翻译的 JSON 文件...", 2);
    QDirIterator iterator(task.modPath,
                          {"*.json"},
                          QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext())
    {
        const QString filePath = iterator.next();
        if (!isExcludedJsonFile(task.modPath, filePath))
            jsonFiles.append(filePath);
    }
    std::sort(jsonFiles.begin(), jsonFiles.end());

    if (jsonFiles.isEmpty())
    {
        result.errorMessage = "未找到可处理的 JSON 文件";
        return result;
    }

    appendLog(QString("实验模式：准备处理 %1 个 JSON 文件。").arg(jsonFiles.size()));
    for (qsizetype index = 0; index < jsonFiles.size(); ++index)
    {
        if (m_cancelRequested.load())
        {
            result.cancelled = true;
            return result;
        }

        const QString targetFile = jsonFiles.at(index);
        const QString backupFile = targetFile + ".stardewtranslator.bak";
        updateStatus(QString("正在处理第 %1/%2 个文件：%3")
                         .arg(index + 1)
                         .arg(jsonFiles.size())
                         .arg(QFileInfo(targetFile).fileName()));
        if (!QFileInfo::exists(backupFile) && !QFile::copy(targetFile, backupFile))
        {
            result.errorMessage = "无法创建原文件备份: " + QDir::toNativeSeparators(backupFile);
            return result;
        }

        appendLog("正在处理: " + QDir::toNativeSeparators(targetFile));
        if (!translateJsonDocument(backupFile,
                                   targetFile,
                                   provider,
                                   task,
                                   false,
                                   result.errorMessage))
        {
            result.cancelled = m_cancelRequested.load();
            return result;
        }

        ++result.processedFileCount;
        updateProgress(static_cast<int>(((index + 1) * 100) / jsonFiles.size()));
    }

    result.success = true;
    return result;
}

bool MainWindow::translateJsonDocument(const QString &sourceFilePath,
                                       const QString &targetFilePath,
                                       TranslationProvider &provider,
                                       const TranslationTask &task,
                                       bool reportBatchProgress,
                                       QString &errorMessage)
{
    const QString displayFileName = QFileInfo(sourceFilePath).fileName();
    updateStatus("正在读取文件：" + displayFileName,
                 reportBatchProgress ? 4 : -1);
    QFile sourceFile(sourceFilePath);
    if (!sourceFile.open(QIODevice::ReadOnly))
    {
        errorMessage = "无法读取文件: " + QDir::toNativeSeparators(sourceFilePath)
                       + "（" + sourceFile.errorString() + "）";
        return false;
    }
    const QByteArray sourceData = sourceFile.readAll();

    updateStatus("正在解析文件：" + displayFileName,
                 reportBatchProgress ? 6 : -1);
    QJsonParseError parseError;
    QJsonDocument sourceDocument = QJsonDocument::fromJson(sourceData, &parseError);
    QByteArray parsedSourceData = sourceData;
    bool usedCompatibilityMode = false;

    // SMAPI/部分 Mod 的“JSON”实际允许注释和尾随逗号。严格解析失败时才清理
    // 这些可安全识别的扩展；缺引号、缺括号等结构损坏仍会被拒绝。
    if (parseError.error != QJsonParseError::NoError)
    {
        const QByteArray compatibleData = JsonUtils::normalizeJsonExtensions(sourceData);
        if (compatibleData != sourceData)
        {
            QJsonParseError compatibleError;
            const QJsonDocument compatibleDocument =
                QJsonDocument::fromJson(compatibleData, &compatibleError);
            parsedSourceData = compatibleData;
            parseError = compatibleError;
            if (compatibleError.error == QJsonParseError::NoError)
            {
                sourceDocument = compatibleDocument;
                usedCompatibilityMode = true;
            }
        }
    }

    if (parseError.error != QJsonParseError::NoError)
    {
        errorMessage = "JSON 解析失败: " + QDir::toNativeSeparators(sourceFilePath)
                       + "（" + jsonParseErrorDescription(parsedSourceData, parseError) + "）";
        return false;
    }
    if (!sourceDocument.isObject() && !sourceDocument.isArray())
    {
        errorMessage = "JSON 根节点必须是对象或数组: "
                       + QDir::toNativeSeparators(sourceFilePath);
        return false;
    }
    if (usedCompatibilityMode)
    {
        appendLog("源文件包含 JSON 注释或尾随逗号，已按兼容模式读取；原文件不会被修改。");
    }

    const QJsonValue rootValue = sourceDocument.isObject()
                                     ? QJsonValue(sourceDocument.object())
                                     : QJsonValue(sourceDocument.array());
    const QStringList sourceTexts = JsonUtils::extractStrings(rootValue);
    if (sourceTexts.isEmpty())
        appendLog("文件中没有非空字符串，将保持原结构写入。");
    else
        appendLog(QString("提取到 %1 条字符串。").arg(sourceTexts.size()));

    updateStatus(sourceTexts.isEmpty()
                     ? QString("文件没有可翻译文本，准备写入：%1").arg(displayFileName)
                     : QString("已提取 %1 条文本，准备分批翻译").arg(sourceTexts.size()),
                 reportBatchProgress ? 10 : -1);

    QStringList translations;
    translations.reserve(sourceTexts.size());
    const qsizetype totalBatches =
        (sourceTexts.size() + TranslationBatchSize - 1) / TranslationBatchSize;
    for (qsizetype offset = 0; offset < sourceTexts.size(); offset += TranslationBatchSize)
    {
        if (m_cancelRequested.load())
        {
            errorMessage = "任务已取消";
            return false;
        }

        const QStringList batch = sourceTexts.mid(offset, TranslationBatchSize);
        const qsizetype batchNumber = offset / TranslationBatchSize + 1;
        const int batchStartProgress = reportBatchProgress
                                           ? 10 + static_cast<int>(
                                                      (offset * 80) / sourceTexts.size())
                                           : -1;
        updateStatus(QString("正在翻译 %1：第 %2/%3 批（本批 %4 条，共 %5 条）")
                         .arg(displayFileName)
                         .arg(batchNumber)
                         .arg(totalBatches)
                         .arg(batch.size())
                         .arg(sourceTexts.size()),
                     batchStartProgress);
        const TranslationBatchResult batchResult =
            provider.translateBatch(batch, task.languageName);
        if (!batchResult.isSuccess())
        {
            errorMessage = batchResult.errorMessage;
            return false;
        }
        if (batchResult.translations.size() != batch.size())
        {
            errorMessage = "模型返回的译文数量与请求数量不一致";
            return false;
        }

        translations.append(batchResult.translations);
        if (reportBatchProgress)
        {
            const qsizetype completed = offset + batch.size();
            updateProgress(10 + static_cast<int>((completed * 80) / sourceTexts.size()));
        }
    }

    updateStatus("正在整理译文：" + displayFileName,
                 reportBatchProgress ? 92 : -1);
    const QJsonValue translatedRoot = JsonUtils::applyTranslations(rootValue, translations);
    const QJsonDocument outputDocument = translatedRoot.isObject()
                                             ? QJsonDocument(translatedRoot.toObject())
                                             : QJsonDocument(translatedRoot.toArray());
    const QByteArray outputData = outputDocument.toJson(QJsonDocument::Indented);

    // QSaveFile 先写同目录临时文件，commit 成功后才原子替换目标文件。
    updateStatus("正在写入文件：" + QFileInfo(targetFilePath).fileName(),
                 reportBatchProgress ? 96 : -1);
    QSaveFile targetFile(targetFilePath);
    targetFile.setDirectWriteFallback(false);
    if (!targetFile.open(QIODevice::WriteOnly))
    {
        errorMessage = "无法创建输出文件: " + QDir::toNativeSeparators(targetFilePath)
                       + "（" + targetFile.errorString() + "）";
        return false;
    }
    if (targetFile.write(outputData) != outputData.size())
    {
        targetFile.cancelWriting();
        errorMessage = "输出文件写入不完整: " + QDir::toNativeSeparators(targetFilePath);
        return false;
    }
    if (!targetFile.commit())
    {
        errorMessage = "无法提交输出文件: " + QDir::toNativeSeparators(targetFilePath)
                       + "（" + targetFile.errorString() + "）";
        return false;
    }

    appendLog("已写入: " + QDir::toNativeSeparators(targetFilePath));
    if (reportBatchProgress)
        updateProgress(99);
    return true;
}

void MainWindow::appendLog(const QString &message)
{
    if (QThread::currentThread() == thread())
    {
        m_logEdit->appendPlainText(QTime::currentTime().toString("[HH:mm:ss] ") + message);
        return;
    }

    QMetaObject::invokeMethod(this,
                              [this, message]() {
                                  m_logEdit->appendPlainText(
                                      QTime::currentTime().toString("[HH:mm:ss] ") + message);
                              },
                              Qt::QueuedConnection);
}

void MainWindow::updateProgress(int value)
{
    const int boundedValue = std::clamp(value, 0, 100);
    if (QThread::currentThread() == thread())
    {
        m_progressBar->setValue(boundedValue);
        return;
    }

    QMetaObject::invokeMethod(this,
                              [this, boundedValue]() { m_progressBar->setValue(boundedValue); },
                              Qt::QueuedConnection);
}

void MainWindow::updateStatus(const QString &step, int progress)
{
    const QString normalizedStep = step.trimmed().isEmpty() ? QString("等待") : step.trimmed();
    const int boundedProgress = progress < 0 ? -1 : std::clamp(progress, 0, 100);
    const auto applyStatus = [this, normalizedStep, boundedProgress]() {
        m_statusLabel->setText(normalizedStep);
        if (boundedProgress >= 0)
            m_progressBar->setValue(boundedProgress);

        QString badgeText;
        QString badgeState;
        if (normalizedStep.contains("失败") || normalizedStep.contains("错误")
            || normalizedStep.contains("无效"))
        {
            badgeText = "需要处理";
            badgeState = "error";
        }
        else if (normalizedStep.contains("完成") || normalizedStep.contains("已写入"))
        {
            badgeText = "已完成";
            badgeState = "success";
        }
        else if (normalizedStep.contains("取消"))
        {
            badgeText = "已取消";
            badgeState = "warning";
        }
        else if (m_isProcessing || normalizedStep.contains("正在"))
        {
            badgeText = "进行中";
            badgeState = "working";
        }
        else if (!m_pendingModPath.isEmpty())
        {
            badgeText = "准备就绪";
            badgeState = "success";
        }
        else
        {
            badgeText = "等待中";
            badgeState = "idle";
        }

        m_statusBadge->setText(badgeText);
        m_statusBadge->setProperty("state", badgeState);
        refreshWidgetStyle(m_statusBadge);
    };

    if (QThread::currentThread() == thread())
    {
        applyStatus();
        return;
    }

    QMetaObject::invokeMethod(this, applyStatus, Qt::QueuedConnection);
}
