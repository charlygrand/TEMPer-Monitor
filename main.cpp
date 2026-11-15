#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <QApplication>
#include <QIcon>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QMessageBox>
#include <QSaveFile>
#include <QDateTime>
#include <QThread>
#include <QTimer>
#include <QPointer>
#include <atomic>
#include <string>
#include <vector>
#include <cmath>
#include <functional>

static HANDLE openSensor() {
    GUID guid{};
    HidD_GetHidGuid(&guid);
    HDEVINFO info = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (info == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
    HANDLE result = INVALID_HANDLE_VALUE;
    for (DWORD n = 0;; ++n) {
        SP_DEVICE_INTERFACE_DATA iface{}; iface.cbSize = sizeof(iface);
        if (!SetupDiEnumDeviceInterfaces(info, nullptr, &guid, n, &iface)) break;
        DWORD bytes = 0;
        SetupDiGetDeviceInterfaceDetailW(info, &iface, nullptr, 0, &bytes, nullptr);
        if (!bytes) continue;
        std::vector<BYTE> storage(bytes);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(info, &iface, detail, bytes, nullptr, nullptr)) continue;
        std::wstring path = detail->DevicePath;
        if (path.find(L"vid_0c45") == std::wstring::npos ||
            path.find(L"pid_7401") == std::wstring::npos ||
            path.find(L"mi_01") == std::wstring::npos) continue;
        result = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        break;
    }
    SetupDiDestroyDeviceInfoList(info);
    return result;
}

static bool transfer(HANDLE handle, bool write, BYTE* data, DWORD length) {
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return false;
    DWORD count = 0;
    BOOL ok = write ? WriteFile(handle, data, length, &count, &ov)
                    : ReadFile(handle, data, length, &count, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, 2000) == WAIT_OBJECT_0)
            ok = GetOverlappedResult(handle, &ov, &count, FALSE);
        else {
            CancelIoEx(handle, &ov);
            GetOverlappedResult(handle, &ov, &count, TRUE);
            ok = FALSE;
        }
    }
    CloseHandle(ov.hEvent);
    return ok && count == length;
}

class SensorThread final : public QThread {
public:
    std::atomic<int> intervalMs{5000};
    std::atomic<bool> stopping{false};
    QPointer<QWidget> receiver;
    std::function<void(double)> onReading;
    std::function<void(QString)> onError;
    void run() override {
        HANDLE sensor = INVALID_HANDLE_VALUE;
        auto reportError = [&](const QString& message) {
            if (!receiver) return;
            QMetaObject::invokeMethod(receiver.data(), [this, message] {
                if (onError) onError(message);
            }, Qt::QueuedConnection);
        };
        while (!stopping.load()) {
            if (sensor == INVALID_HANDLE_VALUE) {
                sensor = openSensor();
                if (sensor == INVALID_HANDLE_VALUE) {
                    reportError("Sensor desconectado — intentando reconectar");
                    for (int t = 0; t < 20 && !stopping.load(); ++t) QThread::msleep(100);
                    continue;
                }
            }
            BYTE command[9] = {0x00, 0x01, 0x80, 0x33, 0x01, 0, 0, 0, 0};
            BYTE reply[9] = {};
            bool good = transfer(sensor, true, command, sizeof(command)) &&
                        transfer(sensor, false, reply, sizeof(reply)) &&
                        reply[1] == 0x80 && reply[2] == 0x02;
            if (good) {
                short raw = static_cast<short>((static_cast<unsigned>(reply[3]) << 8) | reply[4]);
                double c = raw / 256.0;
                if (receiver) QMetaObject::invokeMethod(receiver.data(), [this, c] {
                    if (onReading) onReading(c);
                }, Qt::QueuedConnection);
            } else {
                reportError("Lectura HID fallida — intentando reconectar");
                CloseHandle(sensor);
                sensor = INVALID_HANDLE_VALUE;
            }
            int remaining = intervalMs.load();
            while (remaining > 0 && !stopping.load()) {
                const int slice = remaining < 100 ? remaining : 100;
                QThread::msleep(static_cast<unsigned long>(slice));
                remaining -= slice;
            }
        }
        if (sensor != INVALID_HANDLE_VALUE) CloseHandle(sensor);
    }
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    app.setStyleSheet(R"(
      QWidget { background:#17212d; color:#edf4fa; font-family:'Segoe UI'; font-size:12px; }
      QLabel#heading { font-size:19px; font-weight:700; }
      QLabel#reading { font-size:52px; font-weight:700; color:#f3f8ff; }
      QLabel#muted { color:#a9c0d4; }
      QFrame#panel { background:#223244; border-radius:14px; }
      QComboBox,QLineEdit { background:#34475c; border:1px solid #50647b; border-radius:6px; padding:7px; min-height:22px; }
      QPushButton { background:#386c9a; border:0; border-radius:6px; padding:9px 14px; font-weight:600; }
      QPushButton:hover { background:#4685bb; }
    )");
    // Separate v1.2 settings: never alter the on-air v1.0/v1.1 configuration.
    QSettings settings("TEMPerMonitor", "TEMPerMonitor_v12");
    const QStringList languageCodes = {"es", "en", "pt", "fr", "it", "de"};
    const QStringList languageNames = {"Español", "English", "Português", "Français", "Italiano", "Deutsch"};
    const QVector<QStringList> translations = {
        { QString::fromUtf8("Conectando sensor..."), QString::fromUtf8("Temperatura actual"), QString::fromUtf8("Esperando primera lectura"), QString::fromUtf8("Unidad"), QString::fromUtf8("Intervalo"), QString::fromUtf8("Archivo de salida de temperatura (TXT)"), QString::fromUtf8("Examinar..."), QString::fromUtf8("Esperando lectura USB..."), QString::fromUtf8("Guardar ajustes"), QString::fromUtf8("segundos"), QString::fromUtf8("minutos"), QString::fromUtf8("Sensor conectado"), QString::fromUtf8("Lectura correcta: "), QString::fromUtf8("Última lectura: "), QString::fromUtf8("Sin lectura válida"), QString::fromUtf8("Sensor desconectado — intentando reconectar"), QString::fromUtf8("Lectura HID fallida — intentando reconectar"), QString::fromUtf8("Ruta TXT vacía: no se escribió."), QString::fromUtf8("ERROR al abrir TXT: "), QString::fromUtf8("ERROR al guardar TXT: "), QString::fromUtf8("TXT actualizado: "), QString::fromUtf8("Elegir archivo de temperatura"), QString::fromUtf8("Archivos TXT (*.txt)"), QString::fromUtf8("Configuración"), QString::fromUtf8("Ajustes guardados. El TXT se actualiza en la próxima lectura correcta."), QString::fromUtf8("Idioma") },
        { QString::fromUtf8("Connecting sensor..."), QString::fromUtf8("Current temperature"), QString::fromUtf8("Waiting for first reading"), QString::fromUtf8("Unit"), QString::fromUtf8("Interval"), QString::fromUtf8("Temperature output file (TXT)"), QString::fromUtf8("Browse..."), QString::fromUtf8("Waiting for USB reading..."), QString::fromUtf8("Save settings"), QString::fromUtf8("seconds"), QString::fromUtf8("minutes"), QString::fromUtf8("Sensor connected"), QString::fromUtf8("Successful reading: "), QString::fromUtf8("Last reading: "), QString::fromUtf8("No valid reading"), QString::fromUtf8("Sensor disconnected — reconnecting"), QString::fromUtf8("HID reading failed — reconnecting"), QString::fromUtf8("Empty TXT path: nothing written."), QString::fromUtf8("ERROR opening TXT: "), QString::fromUtf8("ERROR saving TXT: "), QString::fromUtf8("TXT updated: "), QString::fromUtf8("Choose temperature file"), QString::fromUtf8("TXT files (*.txt)"), QString::fromUtf8("Settings"), QString::fromUtf8("Settings saved. TXT updates on the next valid reading."), QString::fromUtf8("Language") },
        { QString::fromUtf8("Conectando sensor..."), QString::fromUtf8("Temperatura atual"), QString::fromUtf8("Aguardando primeira leitura"), QString::fromUtf8("Unidade"), QString::fromUtf8("Intervalo"), QString::fromUtf8("Arquivo de saída de temperatura (TXT)"), QString::fromUtf8("Procurar..."), QString::fromUtf8("Aguardando leitura USB..."), QString::fromUtf8("Salvar configurações"), QString::fromUtf8("segundos"), QString::fromUtf8("minutos"), QString::fromUtf8("Sensor conectado"), QString::fromUtf8("Leitura correta: "), QString::fromUtf8("Última leitura: "), QString::fromUtf8("Sem leitura válida"), QString::fromUtf8("Sensor desconectado — reconectando"), QString::fromUtf8("Falha na leitura HID — reconectando"), QString::fromUtf8("Caminho TXT vazio: nada foi gravado."), QString::fromUtf8("ERRO ao abrir TXT: "), QString::fromUtf8("ERRO ao salvar TXT: "), QString::fromUtf8("TXT atualizado: "), QString::fromUtf8("Escolher arquivo de temperatura"), QString::fromUtf8("Arquivos TXT (*.txt)"), QString::fromUtf8("Configurações"), QString::fromUtf8("Configurações salvas. O TXT será atualizado na próxima leitura válida."), QString::fromUtf8("Idioma") },
        { QString::fromUtf8("Connexion au capteur..."), QString::fromUtf8("Température actuelle"), QString::fromUtf8("En attente de la première mesure"), QString::fromUtf8("Unité"), QString::fromUtf8("Intervalle"), QString::fromUtf8("Fichier de sortie de température (TXT)"), QString::fromUtf8("Parcourir..."), QString::fromUtf8("En attente de mesure USB..."), QString::fromUtf8("Enregistrer"), QString::fromUtf8("secondes"), QString::fromUtf8("minutes"), QString::fromUtf8("Capteur connecté"), QString::fromUtf8("Mesure réussie : "), QString::fromUtf8("Dernière mesure : "), QString::fromUtf8("Aucune mesure valide"), QString::fromUtf8("Capteur déconnecté — reconnexion"), QString::fromUtf8("Échec de lecture HID — reconnexion"), QString::fromUtf8("Chemin TXT vide : aucun fichier écrit."), QString::fromUtf8("ERREUR ouverture TXT : "), QString::fromUtf8("ERREUR enregistrement TXT : "), QString::fromUtf8("TXT mis à jour : "), QString::fromUtf8("Choisir le fichier de température"), QString::fromUtf8("Fichiers TXT (*.txt)"), QString::fromUtf8("Paramètres"), QString::fromUtf8("Paramètres enregistrés. Le TXT sera actualisé à la prochaine mesure valide."), QString::fromUtf8("Langue") },
        { QString::fromUtf8("Connessione al sensore..."), QString::fromUtf8("Temperatura attuale"), QString::fromUtf8("In attesa della prima lettura"), QString::fromUtf8("Unità"), QString::fromUtf8("Intervallo"), QString::fromUtf8("File di uscita temperatura (TXT)"), QString::fromUtf8("Sfoglia..."), QString::fromUtf8("In attesa della lettura USB..."), QString::fromUtf8("Salva impostazioni"), QString::fromUtf8("secondi"), QString::fromUtf8("minuti"), QString::fromUtf8("Sensore connesso"), QString::fromUtf8("Lettura corretta: "), QString::fromUtf8("Ultima lettura: "), QString::fromUtf8("Nessuna lettura valida"), QString::fromUtf8("Sensore scollegato — riconnessione"), QString::fromUtf8("Lettura HID non riuscita — riconnessione"), QString::fromUtf8("Percorso TXT vuoto: nessuna scrittura."), QString::fromUtf8("ERRORE apertura TXT: "), QString::fromUtf8("ERRORE salvataggio TXT: "), QString::fromUtf8("TXT aggiornato: "), QString::fromUtf8("Scegli file temperatura"), QString::fromUtf8("File TXT (*.txt)"), QString::fromUtf8("Impostazioni"), QString::fromUtf8("Impostazioni salvate. Il TXT si aggiornerà alla prossima lettura valida."), QString::fromUtf8("Lingua") },
        { QString::fromUtf8("Sensor wird verbunden..."), QString::fromUtf8("Aktuelle Temperatur"), QString::fromUtf8("Warte auf erste Messung"), QString::fromUtf8("Einheit"), QString::fromUtf8("Intervall"), QString::fromUtf8("Temperatur-Ausgabedatei (TXT)"), QString::fromUtf8("Durchsuchen..."), QString::fromUtf8("Warte auf USB-Messung..."), QString::fromUtf8("Einstellungen speichern"), QString::fromUtf8("Sekunden"), QString::fromUtf8("Minuten"), QString::fromUtf8("Sensor verbunden"), QString::fromUtf8("Messung erfolgreich: "), QString::fromUtf8("Letzte Messung: "), QString::fromUtf8("Keine gültige Messung"), QString::fromUtf8("Sensor getrennt — Verbindung wird wiederhergestellt"), QString::fromUtf8("HID-Lesefehler — erneuter Verbindungsversuch"), QString::fromUtf8("TXT-Pfad leer: nichts geschrieben."), QString::fromUtf8("FEHLER beim Öffnen der TXT: "), QString::fromUtf8("FEHLER beim Speichern der TXT: "), QString::fromUtf8("TXT aktualisiert: "), QString::fromUtf8("Temperaturdatei wählen"), QString::fromUtf8("TXT-Dateien (*.txt)"), QString::fromUtf8("Einstellungen"), QString::fromUtf8("Einstellungen gespeichert. TXT wird bei der nächsten gültigen Messung aktualisiert."), QString::fromUtf8("Sprache") },
    };
    auto* language = new QComboBox;
    language->addItems(languageNames);
    int lang = languageCodes.indexOf(settings.value("language", "es").toString());
    if (lang < 0) lang = 0;
    language->setCurrentIndex(lang);
    auto trText = [&](int key) -> QString { return translations.at(lang).at(key); };
    QWidget window;
    window.setWindowIcon(QIcon("temper.ico"));
    window.resize(610, 480);
    auto* root = new QVBoxLayout(&window);
    root->setContentsMargins(22,22,22,22); root->setSpacing(16);
    auto* top = new QHBoxLayout;
    auto* title = new QLabel("TEMPer Monitor 1.2"); title->setObjectName("heading");
    auto* status = new QLabel; status->setStyleSheet("color:#ffbc76;");
    top->addWidget(title); top->addStretch(); top->addWidget(language); top->addWidget(status); root->addLayout(top);
    auto* panel = new QFrame; panel->setObjectName("panel");
    auto* panelLayout = new QVBoxLayout(panel); panelLayout->setContentsMargins(20,24,20,24);
    auto* heading = new QLabel; heading->setAlignment(Qt::AlignCenter); heading->setObjectName("muted");
    auto* reading = new QLabel("--,- °C"); reading->setObjectName("reading"); reading->setAlignment(Qt::AlignCenter);
    auto* activity = new QLabel; activity->setAlignment(Qt::AlignCenter); activity->setObjectName("muted");
    panelLayout->addWidget(heading); panelLayout->addWidget(reading); panelLayout->addWidget(activity); root->addWidget(panel);
    auto* grid = new QGridLayout;
    auto* units = new QComboBox; units->addItems({"Celsius (°C)", "Fahrenheit (°F)"});
    units->setCurrentIndex(settings.value("units",0).toInt());

auto* interval = new QComboBox;
for (int seconds = 5; seconds <= 60; seconds += 5)
    interval->addItem(QString(), seconds);

for (int minutes = 2; minutes <= 60; ++minutes)
    interval->addItem(QString(), minutes * 60);

int selected = interval->findData(settings.value("interval",5).toInt());
interval->setCurrentIndex(selected < 0 ? 0 : selected);
    auto* calibration = new QComboBox;
    for (int halfDegrees = -20; halfDegrees <= 20; ++halfDegrees) {
        const double degrees = halfDegrees / 2.0;
        const QString prefix = halfDegrees > 0 ? "+" : "";
        calibration->addItem(prefix + QString::number(degrees, 'f', 1).replace('.', ',')
                             + QString::fromUtf8(" °C"), halfDegrees);
    }
    int savedOffset = settings.value("calibrationHalfDegrees", 0).toInt();
    if (savedOffset < -20 || savedOffset > 20) savedOffset = 0;
    calibration->setCurrentIndex(calibration->findData(savedOffset));
    auto* calibrationLabel = new QLabel;
    auto* unitsLabel = new QLabel; auto* intervalLabel = new QLabel;
    grid->addWidget(unitsLabel,0,0); grid->addWidget(intervalLabel,0,1);
    grid->addWidget(units,1,0); grid->addWidget(interval,1,1);
    grid->addWidget(calibrationLabel,2,0,1,2);
    grid->addWidget(calibration,3,0,1,2);
    root->addLayout(grid);
    auto* pathLabel = new QLabel; root->addWidget(pathLabel);
    auto* pathRow = new QHBoxLayout;
    auto* path = new QLineEdit(settings.value("path", QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath("TEMPERATURA_V12_PRUEBA.TXT")).toString());
    auto* browse = new QPushButton;
    pathRow->addWidget(path,1); pathRow->addWidget(browse); root->addLayout(pathRow);
    auto* bottom = new QHBoxLayout;
    auto* notice = new QLabel; notice->setObjectName("muted");
    auto* save = new QPushButton;
    bottom->addWidget(notice); bottom->addStretch(); bottom->addWidget(save); root->addLayout(bottom);
    SensorThread worker;
    worker.receiver = &window;
    worker.intervalMs = interval->currentData().toInt()*1000;
    bool hasReading = false;
    double lastC = 0;
    enum class SensorState { Connecting, Connected, Disconnected, HidError };
    SensorState sensorState = SensorState::Connecting;
    enum class ActivityState { Waiting, Success, Last, Invalid };
    ActivityState activityState = ActivityState::Waiting;
    enum class FileState { Waiting, Updated, Empty, OpenError, SaveError };
    FileState fileState = FileState::Waiting;
    QString activityTime, fileTime, fileError;
    auto formatValue = [&]() {
        const double correctedC = lastC + calibration->currentData().toInt() / 2.0;
        double value = units->currentIndex() == 0 ? correctedC : correctedC*9.0/5.0+32.0;
        QString result = QString::number(value, 'f', 1).replace('.', ',');
        return result + (units->currentIndex() == 0 ? QString::fromUtf8("°C") : QString::fromUtf8("°F"));
    };
    auto refreshTexts = [&]() {
        window.setWindowTitle("TEMPer Monitor 1.2");
        heading->setText(trText(1).toUpper());
        unitsLabel->setText(trText(3)); intervalLabel->setText(trText(4));
        pathLabel->setText(trText(5)); browse->setText(trText(6)); save->setText(trText(8));
        const QStringList calibrationLabels = {
            QString::fromUtf8("Corrección de temperatura (°C)"),
            QString::fromUtf8("Temperature correction (°C)"),
            QString::fromUtf8("Correção de temperatura (°C)"),
            QString::fromUtf8("Correction de température (°C)"),
            QString::fromUtf8("Correzione temperatura (°C)"),
            QString::fromUtf8("Temperaturkorrektur (°C)")
        };
        calibrationLabel->setText(calibrationLabels.at(lang));
        for (int i = 0; i < interval->count(); ++i) {
            int seconds = interval->itemData(i).toInt();
            interval->setItemText(i, seconds <= 60 ? QString("%1 %2").arg(seconds).arg(trText(9))
                                                     : QString("%1 %2").arg(seconds/60).arg(trText(10)));
        }
        QString statusText;
        switch (sensorState) {
            case SensorState::Connecting: statusText = trText(0); break;
            case SensorState::Connected: statusText = trText(11); break;
            case SensorState::Disconnected: statusText = trText(15); break;
            case SensorState::HidError: statusText = trText(16); break;
        }
        status->setText(QString::fromUtf8("● ") + statusText);
        QString activityText;
        switch (activityState) {
            case ActivityState::Waiting: activityText = trText(2); break;
            case ActivityState::Success: activityText = trText(12) + activityTime; break;
            case ActivityState::Last: activityText = trText(13) + activityTime; break;
            case ActivityState::Invalid: activityText = trText(14); break;
        }
        activity->setText(QString::fromUtf8("●  ") + activityText);
        switch (fileState) {
            case FileState::Waiting: notice->setText(trText(7)); break;
            case FileState::Updated: notice->setText(trText(20) + fileTime); break;
            case FileState::Empty: notice->setText(trText(17)); break;
            case FileState::OpenError: notice->setText(trText(18) + fileError); break;
            case FileState::SaveError: notice->setText(trText(19) + fileError); break;
        }
    };
    auto writeTxt = [&]() {
        const QString filePath = path->text().trimmed();
        if (filePath.isEmpty()) { fileState = FileState::Empty; refreshTexts(); return; }
        QSaveFile file(filePath);
        if (!file.open(QIODevice::WriteOnly)) {
            fileState = FileState::OpenError; fileError = file.errorString(); refreshTexts(); return;
        }
        // Deliberately preserve v1.0's one-line, no-BOM Latin-1/Windows-1252-compatible output.
        const QByteArray bytes = formatValue().toLatin1();
        if (file.write(bytes) != bytes.size() || !file.commit()) {
            fileState = FileState::SaveError; fileError = file.errorString(); refreshTexts(); return;
        }
        fileState = FileState::Updated;
        fileTime = QDateTime::currentDateTime().toString("HH:mm:ss");
        refreshTexts();
    };
    QObject::connect(browse,&QPushButton::clicked,&window,[&]{
        const QString selectedPath = QFileDialog::getSaveFileName(&window,trText(21),path->text(),trText(22));
        if (!selectedPath.isEmpty()) path->setText(selectedPath);
    });
    QTimer ledTimer; ledTimer.setSingleShot(true);
    QObject::connect(&ledTimer,&QTimer::timeout,&window,[&]{
        activityState = ActivityState::Last;
        refreshTexts(); activity->setStyleSheet("color:#a9c0d4;");
    });
    worker.onReading = [&](double c) {
        hasReading = true; lastC = c;
        reading->setText(formatValue());
        sensorState = SensorState::Connected;
        status->setStyleSheet("color:#20cb79;");
        activityState = ActivityState::Success;
        activityTime = QDateTime::currentDateTime().toString("HH:mm:ss");
        activity->setStyleSheet("color:#20cb79;");
        ledTimer.start(500);
        writeTxt();
    };
    worker.onError = [&](QString message) {
        sensorState = message.startsWith("Sensor desconectado") ? SensorState::Disconnected : SensorState::HidError;
        status->setStyleSheet("color:#ff7373;");
        activityState = ActivityState::Invalid;
        activity->setStyleSheet("color:#ff7373;");
        ledTimer.stop();
        refreshTexts();
    };
    QObject::connect(interval,&QComboBox::currentIndexChanged,&window,[&](int){
        worker.intervalMs = interval->currentData().toInt()*1000;
    });
    QObject::connect(units,&QComboBox::currentIndexChanged,&window,[&](int){
        if (hasReading) reading->setText(formatValue());
    });
    QObject::connect(calibration,&QComboBox::currentIndexChanged,&window,[&](int){
        if (hasReading) reading->setText(formatValue());
        // The corrected TXT is written on the next successful sensor reading.
    });
    QObject::connect(language,&QComboBox::currentIndexChanged,&window,[&](int index){
        lang = index;
        settings.setValue("language",languageCodes.at(lang));
        refreshTexts();
    });
    QObject::connect(save,&QPushButton::clicked,&window,[&]{
        settings.setValue("units",units->currentIndex());
        settings.setValue("interval",interval->currentData().toInt());
        settings.setValue("calibrationHalfDegrees",calibration->currentData().toInt());
        settings.setValue("path",path->text());
        settings.setValue("language",languageCodes.at(lang));
        settings.sync();
        QMessageBox::information(&window,trText(23),trText(24));
    });
    QObject::connect(&app,&QCoreApplication::aboutToQuit,&window,[&]{
        worker.stopping = true;
        worker.wait();
    });
    refreshTexts();
    window.show();
    worker.start();
    return app.exec();
}
