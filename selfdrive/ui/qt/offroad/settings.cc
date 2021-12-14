#include "selfdrive/ui/qt/offroad/settings.h"

#include <cassert>
#include <cmath>
#include <string>

#include <QDebug>

#ifndef QCOM
#include "selfdrive/ui/qt/offroad/networking.h"
#endif

#ifdef ENABLE_MAPS
#include "selfdrive/ui/qt/maps/map_settings.h"
#endif

#include "selfdrive/common/params.h"
#include "selfdrive/common/util.h"
#include "selfdrive/hardware/hw.h"
#include "selfdrive/ui/qt/widgets/controls.h"
#include "selfdrive/ui/qt/widgets/input.h"
#include "selfdrive/ui/qt/widgets/scrollview.h"
#include "selfdrive/ui/qt/widgets/ssh_keys.h"
#include "selfdrive/ui/qt/widgets/toggle.h"
#include "selfdrive/ui/ui.h"
#include "selfdrive/ui/qt/util.h"
#include "selfdrive/ui/qt/qt_window.h"

#include <QProcess> // opkr
#include <QDateTime> // opkr
#include <QTimer> // opkr
#include <QComboBox>
#include <QAbstractItemView>
#include <QScroller>
#include <QListView>
#include <QListWidget>

TogglesPanel::TogglesPanel(SettingsWindow *parent) : ListWidget(parent) {
  // param, title, desc, icon
  std::vector<std::tuple<QString, QString, QString, QString>> toggles{
    {
      "OpenpilotEnabledToggle",
      "Enable openpilot",
      "Use the openpilot system for adaptive cruise control and lane keep driver assistance. Your attention is required at all times to use this feature. Changing this setting takes effect when the car is powered off.",
      "../assets/offroad/icon_openpilot.png",
    },
    {
      "IsLdwEnabled",
      "Enable Lane Departure Warnings",
      "Receive alerts to steer back into the lane when your vehicle drifts over a detected lane line without a turn signal activated while driving over 31mph (50kph).",
      "../assets/offroad/icon_warning.png",
    },
    {
      "IsRHD",
      "Enable Right-Hand Drive",
      "Allow openpilot to obey left-hand traffic conventions and perform driver monitoring on right driver seat.",
      "../assets/offroad/icon_openpilot_mirrored.png",
    },
    {
      "IsMetric",
      "Use Metric System",
      "Display speed in km/h instead of mph.",
      "../assets/offroad/icon_metric.png",
    },
    {
      "CommunityFeaturesToggle",
      "Enable Community Features",
      "Use features, such as community supported hardware, from the open source community that are not maintained or supported by comma.ai and have not been confirmed to meet the standard safety model. Be extra cautious when using these features",
      "../assets/offroad/icon_shell.png",
    },
    {
      "RecordFront",
      "Record and Upload Driver Camera",
      "Upload data from the driver facing camera and help improve the driver monitoring algorithm.",
      "../assets/offroad/icon_monitoring.png",
    },
    {
      "EndToEndToggle",
      "\U0001f96c Disable use of lanelines (Alpha) \U0001f96c",
      "In this mode openpilot will ignore lanelines and just drive how it thinks a human would.",
      "../assets/offroad/icon_road.png",
    },
#ifdef ENABLE_MAPS
    {
      "NavSettingTime24h",
      "Show ETA in 24h format",
      "Use 24h format instead of am/pm",
      "../assets/offroad/icon_metric.png",
    },
#endif

  };

  Params params;

  if (params.getBool("DisableRadar_Allow")) {
    toggles.push_back({
      "DisableRadar",
      "openpilot Longitudinal Control",
      "openpilot will disable the car's radar and will take over control of gas and brakes. Warning: this disables AEB!",
      "../assets/offroad/icon_speed_limit.png",
    });
  }

  for (auto &[param, title, desc, icon] : toggles) {
    auto toggle = new ParamControl(param, title, desc, icon, this);
    bool locked = params.getBool((param + "Lock").toStdString());
    toggle->setEnabled(!locked);
    //if (!locked) {
    //  connect(parent, &SettingsWindow::offroadTransition, toggle, &ParamControl::setEnabled);
    //}
    addItem(toggle);
  }
}

DevicePanel::DevicePanel(SettingsWindow *parent) : ListWidget(parent) {
  Params params = Params();

  setSpacing(30);
  addItem(new LabelControl("Dongle ID", getDongleId().value_or("N/A")));
  addItem(new LabelControl("Serial", params.get("HardwareSerial").c_str()));
  
  // Openpilot View
  addItem(new OpenpilotView());
  
  // soft reboot button
  QHBoxLayout *reset_layout = new QHBoxLayout(); //새로운 버튼 추가를 위한 레이아웃 변수 reset
  reset_layout->setSpacing(30);

  QPushButton *restart_openpilot_btn = new QPushButton("소프트 재부팅");
  restart_openpilot_btn->setStyleSheet("height: 120px;border-radius: 15px;background-color: #7300a4;");
  reset_layout->addWidget(restart_openpilot_btn);
  QObject::connect(restart_openpilot_btn, &QPushButton::released, [=]() {
    emit closeSettings();
    QTimer::singleShot(1000, []() {
      Params().putBool("SoftRestartTriggered", true);
    });
  });
  
  // reset calibration button
  QPushButton *reset_calib_btn = new QPushButton("캘리 및 학습값 초기화");
  reset_calib_btn->setStyleSheet("height: 120px;border-radius: 15px;background-color: #008299;");
  reset_layout->addWidget(reset_calib_btn);
  QObject::connect(reset_calib_btn, &QPushButton::released, [=]() {
    if (ConfirmationDialog::confirm("캘리브레이션 및 학습값을 초기화 할까요?", this)) {
      Params().remove("CalibrationParams");
      Params().remove("LiveParameters");
      emit closeSettings();
      QTimer::singleShot(1000, []() {
        Params().putBool("SoftRestartTriggered", true);
      });
    }
  });

  addItem(reset_layout);

 // power buttons
  QHBoxLayout *power_layout = new QHBoxLayout();
  power_layout->setSpacing(30);

  QPushButton *reboot_btn = new QPushButton("재부팅");
  reboot_btn->setObjectName("reboot_btn");
  power_layout->addWidget(reboot_btn);
  QObject::connect(reboot_btn, &QPushButton::clicked, this, &DevicePanel::reboot);

  QPushButton *poweroff_btn = new QPushButton("종료");
  poweroff_btn->setObjectName("poweroff_btn");
  power_layout->addWidget(poweroff_btn);
  QObject::connect(poweroff_btn, &QPushButton::clicked, this, &DevicePanel::poweroff);

  setStyleSheet(R"(
    QPushButton {
      height: 120px;
      border-radius: 15px;
    }
    #reboot_btn { background-color: #00bfff; }
    #reboot_btn:pressed { background-color: #0000ff; }
    #poweroff_btn { background-color: #E22C2C; }
    #poweroff_btn:pressed { background-color: #FF2424; }
  )");
  addItem(power_layout);
    
  // offroad-only buttons

  auto dcamBtn = new ButtonControl("운전자 모니터링 미리보기", "실행",
                                   "운전자 모니터링 카메라를 미리보고 최적의 장착위치를 찾아보세요.");
  connect(dcamBtn, &ButtonControl::clicked, [=]() { emit showDriverView(); });
  addItem(dcamBtn);

  auto resetCalibBtn = new ButtonControl("캘리브레이션 리셋", "실행", " ");
  connect(resetCalibBtn, &ButtonControl::showDescription, this, &DevicePanel::updateCalibDescription);
  connect(resetCalibBtn, &ButtonControl::clicked, [&]() {
    if (ConfirmationDialog::confirm("캘리브레이션 리셋을 실행하시겠습니까?", this)) {
      params.remove("CalibrationParams");
    }
  });
  addItem(resetCalibBtn);

  if (!params.getBool("Passive")) {
    auto retrainingBtn = new ButtonControl("트레이닝 가이드", "실행", "Review the rules, features, and limitations of openpilot");
    connect(retrainingBtn, &ButtonControl::clicked, [=]() {
      if (ConfirmationDialog::confirm("트레이닝 가이드를 실행하시겠습니까?", this)) {
        emit reviewTrainingGuide();
      }
    });
    addItem(retrainingBtn);
  }

  if (Hardware::TICI()) {
    auto regulatoryBtn = new ButtonControl("주의사항", "보기", "");
    connect(regulatoryBtn, &ButtonControl::clicked, [=]() {
      const std::string txt = util::read_file("../assets/offroad/fcc.html");
      RichTextDialog::alert(QString::fromStdString(txt), this);
    });
    addItem(regulatoryBtn);
  }

  QObject::connect(parent, &SettingsWindow::offroadTransition, [=](bool offroad) {
    //for (auto btn : findChildren<ButtonControl *>()) {
    //  btn->setEnabled(offroad);
    //}
  });
}

void DevicePanel::updateCalibDescription() {
  QString desc =
      "오픈파일럿은 좌우로 4° 위아래로 5° 를 보정합니다."
      "그 이상의 경우 보정이 필요합니다.";
  std::string calib_bytes = Params().get("CalibrationParams");
  if (!calib_bytes.empty()) {
    try {
      AlignedBuffer aligned_buf;
      capnp::FlatArrayMessageReader cmsg(aligned_buf.align(calib_bytes.data(), calib_bytes.size()));
      auto calib = cmsg.getRoot<cereal::Event>().getLiveCalibration();
      if (calib.getCalStatus() != 0) {
        double pitch = calib.getRpyCalib()[1] * (180 / M_PI);
        double yaw = calib.getRpyCalib()[2] * (180 / M_PI);
        desc += QString(" 장치 장착상태는 %1° %2 그리고 %3° %4.")
                    .arg(QString::number(std::abs(pitch), 'g', 1), pitch > 0 ? "위로" : "아래로",
                         QString::number(std::abs(yaw), 'g', 1), yaw > 0 ? "우측으로" : "");
      }
    } catch (kj::Exception) {
      qInfo() << "invalid CalibrationParams";
    }
  }
  qobject_cast<ButtonControl *>(sender())->setDescription(desc);
}

void DevicePanel::reboot() {
  if (QUIState::ui_state.status == UIStatus::STATUS_DISENGAGED) {
    if (ConfirmationDialog::confirm("재부팅 하시겠습니까?", this)) {
      // Check engaged again in case it changed while the dialog was open
      if (QUIState::ui_state.status == UIStatus::STATUS_DISENGAGED) {
        Params().putBool("DoReboot", true);
      }
    }
  } else {
    ConfirmationDialog::alert("Disengage to Reboot", this);
  }
}

void DevicePanel::poweroff() {
  if (QUIState::ui_state.status == UIStatus::STATUS_DISENGAGED) {
    if (ConfirmationDialog::confirm("종료하시겠습?", this)) {
      // Check engaged again in case it changed while the dialog was open
      if (QUIState::ui_state.status == UIStatus::STATUS_DISENGAGED) {
        Params().putBool("DoShutdown", true);
      }
    }
  } else {
    ConfirmationDialog::alert("Disengage to Power Off", this);
  }
}

SoftwarePanel::SoftwarePanel(QWidget* parent) : ListWidget(parent) {
  gitBranchLbl = new LabelControl("Git Branch");
  gitCommitLbl = new LabelControl("Git Commit");
  osVersionLbl = new LabelControl("OS Version");
  versionLbl = new LabelControl("Version", "", QString::fromStdString(params.get("ReleaseNotes")).trimmed());
  lastUpdateLbl = new LabelControl("Last Update Check", "", "The last time openpilot successfully checked for an update. The updater only runs while the car is off.");
  updateBtn = new ButtonControl("Check for Update", "");
  connect(updateBtn, &ButtonControl::clicked, [=]() {
    if (params.getBool("IsOffroad")) {
      fs_watch->addPath(QString::fromStdString(params.getParamPath("LastUpdateTime")));
      fs_watch->addPath(QString::fromStdString(params.getParamPath("UpdateFailedCount")));
      updateBtn->setText("CHECKING");
      updateBtn->setEnabled(false);
    }
    std::system("pkill -1 -f selfdrive.updated");
  });


  auto uninstallBtn = new ButtonControl("오픈파일럿 삭제 " + getBrand(), "삭제");
  connect(uninstallBtn, &ButtonControl::clicked, [&]() {
    if (ConfirmationDialog::confirm("오픈파일럿을 삭제하시겠습니까?", this)) {
      params.putBool("DoUninstall", true);
    }
  });
  connect(parent, SIGNAL(offroadTransition(bool)), uninstallBtn, SLOT(setEnabled(bool)));

  QWidget *widgets[] = {versionLbl, lastUpdateLbl, updateBtn, gitBranchLbl, gitCommitLbl, osVersionLbl, uninstallBtn};
  for (QWidget* w : widgets) {
    addItem(w);
  }

  fs_watch = new QFileSystemWatcher(this);
  QObject::connect(fs_watch, &QFileSystemWatcher::fileChanged, [=](const QString path) {
    if (path.contains("UpdateFailedCount") && std::atoi(params.get("UpdateFailedCount").c_str()) > 0) {
      lastUpdateLbl->setText("failed to fetch update");
      updateBtn->setText("CHECK");
      updateBtn->setEnabled(true);
    } else if (path.contains("LastUpdateTime")) {
      updateLabels();
    }
  });
}

void SoftwarePanel::showEvent(QShowEvent *event) {
  updateLabels();
}

void SoftwarePanel::updateLabels() {
  QString lastUpdate = "";
  auto tm = params.get("LastUpdateTime");
  if (!tm.empty()) {
    lastUpdate = timeAgo(QDateTime::fromString(QString::fromStdString(tm + "Z"), Qt::ISODate));
  }

  versionLbl->setText(getBrandVersion());
  lastUpdateLbl->setText(lastUpdate);
  updateBtn->setText("CHECK");
  updateBtn->setEnabled(true);
  gitBranchLbl->setText(QString::fromStdString(params.get("GitBranch")));
  gitCommitLbl->setText(QString::fromStdString(params.get("GitCommit")).left(10));
  osVersionLbl->setText(QString::fromStdString(Hardware::get_os_version()).trimmed());
}

QWidget * network_panel(QWidget * parent) {
#ifdef QCOM
  QWidget *w = new QWidget(parent);
  QVBoxLayout *layout = new QVBoxLayout(w);
  layout->setContentsMargins(50, 0, 50, 0);

  ListWidget *list = new ListWidget();
  list->setSpacing(30);
  // wifi + tethering buttons
  auto wifiBtn = new ButtonControl("\U0001f4f6 WiFi 설정", "열기");
  QObject::connect(wifiBtn, &ButtonControl::clicked, [=]() { HardwareEon::launch_wifi(); });
  list->addItem(wifiBtn);

  auto tetheringBtn = new ButtonControl("\U0001f4f6 테더링 설정", "열기");
  QObject::connect(tetheringBtn, &ButtonControl::clicked, [=]() { HardwareEon::launch_tethering(); });
  list->addItem(tetheringBtn);

  // SSH key management
  list->addItem(new SshToggle());
  list->addItem(new SshControl());
  list->addItem(horizontal_line());
  // add
  const char* gitpull = "sh /data/openpilot/gitpull.sh";
  auto gitpullbtn = new ButtonControl("GitPull", "실행");
  QObject::connect(gitpullbtn, &ButtonControl::clicked, [=]() {
    if (ConfirmationDialog::confirm("GitPull 실행하시겠습니까?", w)){
      std::system(gitpull);
      QTimer::singleShot(1000, []() { Hardware::reboot(); });
    }
  });
  list->addItem(gitpullbtn);
  list->addItem(horizontal_line());

  layout->addWidget(list);
  layout->addStretch(1);
#else
  Networking *w = new Networking(parent);
#endif
  return w;
}
//VIP menu
VIPPanel::VIPPanel(QWidget* parent) : QWidget(parent) {
  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->addWidget(new LabelControl("UI설정", ""));
  layout->addWidget(horizontal_line());
  layout->addWidget(new LabelControl("제어메뉴", ""));
  layout->addWidget(new LateralControlSelect());
  layout->addWidget(new ParamControl("C3MdpsSet",
                                            "C3 MDPS 버스1 고정",
                                            "콤마3 MDPS 개조 버스1로 고정",
                                            "../assets/offroad/icon_road.png",
                                            this));
  layout->addWidget(new ParamControl("WarningOverSpeedLimit",
                                            "Warning when speed limit is exceeded.",
                                            "",
                                            "../assets/offroad/icon_openpilot.png",
                                            this));
  layout->addWidget(new ParamControl("SteerLockout",
                                            "Enable Higher Than 90° Lateral Control",
                                            "This disables the max steer limit of 90°.This will cause fault on certain cars that have a 90° limit.",
                                            "../assets/offroad/icon_road.png",
                                            this));
  
  layout->addWidget(horizontal_line());
}

static QStringList get_list(const char* path)
{
  QStringList stringList;
  QFile textFile(path);
  if(textFile.open(QIODevice::ReadOnly))
  {
      QTextStream textStream(&textFile);
      while (true)
      {
        QString line = textStream.readLine();
        if (line.isNull())
            break;
        else
            stringList.append(line);
      }
  }

  return stringList;
}

void SettingsWindow::showEvent(QShowEvent *event) {
  panel_widget->setCurrentIndex(0);
  nav_btns->buttons()[0]->setChecked(true);
}

SettingsWindow::SettingsWindow(QWidget *parent) : QFrame(parent) {

  // setup two main layouts
  sidebar_widget = new QWidget;
  QVBoxLayout *sidebar_layout = new QVBoxLayout(sidebar_widget);
  sidebar_layout->setMargin(0);
  panel_widget = new QStackedWidget();
  panel_widget->setStyleSheet(R"(
    border-radius: 30px;
    background-color: #292929;
  )");

  // close button
  QPushButton* close_btn = new QPushButton("← 닫기");
  close_btn->setStyleSheet(R"(
    QPushButton {
      font-size: 50px;
      font-weight: bold;
      margin: 0px;
      padding: 15px;
      border-width: 0;
      border-radius: 30px;
      color: #dddddd;
      background-color: #0100ff;
    }
  )");
  close_btn->setFixedSize(300, 110);
  sidebar_layout->addSpacing(10);
  sidebar_layout->addWidget(close_btn, 0, Qt::AlignRight);
  sidebar_layout->addSpacing(10);
  QObject::connect(close_btn, &QPushButton::clicked, this, &SettingsWindow::closeSettings);

  // setup panels
  DevicePanel *device = new DevicePanel(this);
  QObject::connect(device, &DevicePanel::reviewTrainingGuide, this, &SettingsWindow::reviewTrainingGuide);
  QObject::connect(device, &DevicePanel::showDriverView, this, &SettingsWindow::showDriverView);
  QObject::connect(device, &DevicePanel::closeSettings, this, &SettingsWindow::closeSettings);

  QList<QPair<QString, QWidget *>> panels = {
    {"장치", device},
    {"VIP메뉴", new VIPPanel(this)},
    {"네트워크", network_panel(this)},
    {"토글메뉴", new TogglesPanel(this)},
    {"소프트웨어", new SoftwarePanel(this)},
    {"커뮤니티", new CommunityPanel(this)},
  };

#ifdef ENABLE_MAPS
  auto map_panel = new MapPanel(this);
  panels.push_back({"Navigation", map_panel});
  QObject::connect(map_panel, &MapPanel::closeSettings, this, &SettingsWindow::closeSettings);
#endif

  const int padding = panels.size() > 3 ? 25 : 35;

  nav_btns = new QButtonGroup(this);
  for (auto &[name, panel] : panels) {
    QPushButton *btn = new QPushButton(name);
    btn->setCheckable(true);
    btn->setChecked(nav_btns->buttons().size() == 0);
    btn->setStyleSheet(QString(R"(
      QPushButton {
        color: grey;
        border: none;
        background: none;
        font-size: 60px;
        font-weight: 500;
        padding-top: %1px;
        padding-bottom: %1px;
      }
      QPushButton:checked {
        color: white;
      }
      QPushButton:pressed {
        color: #ADADAD;
      }
    )").arg(padding));

    nav_btns->addButton(btn);
    sidebar_layout->addWidget(btn, 0, Qt::AlignRight);

    const int lr_margin = name != "Network" ? 50 : 0;  // Network panel handles its own margins
    panel->setContentsMargins(lr_margin, 25, lr_margin, 25);

    ScrollView *panel_frame = new ScrollView(panel, this);
    panel_widget->addWidget(panel_frame);

    QObject::connect(btn, &QPushButton::clicked, [=, w = panel_frame]() {
      btn->setChecked(true);
      panel_widget->setCurrentWidget(w);
    });
  }
  sidebar_layout->setContentsMargins(50, 50, 100, 50);

  // main settings layout, sidebar + main panel
  QHBoxLayout *main_layout = new QHBoxLayout(this);

  sidebar_widget->setFixedWidth(500);
  main_layout->addWidget(sidebar_widget);
  main_layout->addWidget(panel_widget);

  setStyleSheet(R"(
    * {
      color: white;
      font-size: 50px;
    }
    SettingsWindow {
      background-color: black;
    }
  )");
}

void SettingsWindow::hideEvent(QHideEvent *event) {
#ifdef QCOM
  HardwareEon::close_activities();
#endif
}


/////////////////////////////////////////////////////////////////////////

CommunityPanel::CommunityPanel(QWidget* parent) : QWidget(parent) {

  main_layout = new QStackedLayout(this);

  QWidget* homeScreen = new QWidget(this);
  QVBoxLayout* vlayout = new QVBoxLayout(homeScreen);
  vlayout->setContentsMargins(0, 20, 0, 20);

  QString selected = QString::fromStdString(Params().get("SelectedCar"));

  QPushButton* selectCarBtn = new QPushButton(selected.length() ? selected : "Select your car");
  selectCarBtn->setObjectName("selectCarBtn");
  selectCarBtn->setStyleSheet("margin-right: 30px;");
  //selectCarBtn->setFixedSize(350, 100);
  connect(selectCarBtn, &QPushButton::clicked, [=]() { main_layout->setCurrentWidget(selectCar); });
  vlayout->addSpacing(10);
  vlayout->addWidget(selectCarBtn, 0, Qt::AlignRight);
  vlayout->addSpacing(10);

  homeWidget = new QWidget(this);
  QVBoxLayout* toggleLayout = new QVBoxLayout(homeWidget);
  homeWidget->setObjectName("homeWidget");

  ScrollView *scroller = new ScrollView(homeWidget, this);
  scroller->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  vlayout->addWidget(scroller, 1);

  main_layout->addWidget(homeScreen);

  selectCar = new SelectCar(this);
  connect(selectCar, &SelectCar::backPress, [=]() { main_layout->setCurrentWidget(homeScreen); });
  connect(selectCar, &SelectCar::selectedCar, [=]() {

     QString selected = QString::fromStdString(Params().get("SelectedCar"));
     selectCarBtn->setText(selected.length() ? selected : "Select your car");
     main_layout->setCurrentWidget(homeScreen);
  });
  main_layout->addWidget(selectCar);

  QPalette pal = palette();
  pal.setColor(QPalette::Background, QColor(0x29, 0x29, 0x29));
  setAutoFillBackground(true);
  setPalette(pal);

  setStyleSheet(R"(
    #back_btn, #selectCarBtn {
      font-size: 50px;
      margin: 0px;
      padding: 20px;
      border-width: 0;
      border-radius: 30px;
      color: #dddddd;
      background-color: #444444;
    }
  )");

  QList<ParamControl*> toggles;
  
  toggles.append(new ParamControl("PutPrebuilt", 
                                           "Smart Prebuilt 실행 ",
                                           "Prebuilt 파일을 생성하며 부팅속도를 향상시킵니다.",
                                            "../assets/offroad/icon_shell.png",
                                            this));

  toggles.append(new ParamControl("UseClusterSpeed",
                                            "계기판 속도 사용",
                                            "Use cluster speed instead of wheel speed.",
                                            "../assets/offroad/icon_road.png",
                                            this));

  toggles.append(new ParamControl("LongControlEnabled",
                                            "HKG 롱컨트롤 사용",
                                            "warnings: it is beta, be careful!! Openpilot will control the speed of your car",
                                            "../assets/offroad/icon_road.png",
                                            this));

  toggles.append(new ParamControl("MadModeEnabled",
                                            "HKG MAD 모드 ",
                                            "Openpilot will engage when turn cruise control on",
                                            "../assets/offroad/icon_openpilot.png",
                                            this));

  /*toggles.append(new ParamControl("IsLdwsCar",
                                            "LDWS",
                                            "If your car only supports LDWS, turn it on.",
                                            "../assets/offroad/icon_openpilot.png",
                                            this));*/

  toggles.append(new ParamControl("LaneChangeEnabled",
                                            "Enable Lane Change Assist",
                                            "Perform assisted lane changes with openpilot by checking your surroundings for safety, activating the turn signal and gently nudging the steering wheel towards your desired lane. openpilot is not capable of checking if a lane change is safe. You must continuously observe your surroundings to use this feature.",
                                            "../assets/offroad/icon_road.png",
                                            this));

  toggles.append(new ParamControl("AutoLaneChangeEnabled",
                                            "Enable Auto Lane Change(Nudgeless)",
                                            "warnings: it is beta, be careful!!",
                                            "../assets/offroad/icon_road.png",
                                            this));

  toggles.append(new ParamControl("SccSmootherSlowOnCurves",
                                            "Enable Slow On Curves",
                                            "",
                                            "../assets/offroad/icon_road.png",
                                            this));

  toggles.append(new ParamControl("SccSmootherSyncGasPressed",
                                            "Sync set speed on gas pressed",
                                            "",
                                            "../assets/offroad/icon_road.png",
                                            this));

  /*toggles.append(new ParamControl("StockNaviDecelEnabled",
                                            "Stock Navi based deceleration",
                                            "Use the stock navi based deceleration for longcontrol",
                                            "../assets/offroad/icon_road.png",
                                            this));*/

  toggles.append(new ParamControl("KeepSteeringTurnSignals",
                                            "Keep steering while turn signals.",
                                            "",
                                            "../assets/offroad/icon_openpilot.png",
                                            this));

  /*toggles.append(new ParamControl("NewRadarInterface",
                                            "Use new radar interface",
                                            "",
                                            "../assets/offroad/icon_road.png",
                                            this));*/

  /*toggles.append(new ParamControl("DisableOpFcw",
                                            "Disable Openpilot FCW",
                                            "",
                                            "../assets/offroad/icon_shell.png",
                                            this));*/
  
  toggles.append(new ParamControl("CustomLeadMark",
                                            "Use custom lead mark",
                                            "",
                                            "../assets/offroad/icon_shell.png",
                                            this));

  /*toggles.append(new ParamControl("CustomLeadMark",
                                            "Use custom lead mark",
                                            "",
                                            "../assets/offroad/icon_road.png",
                                            this));*/

  for(ParamControl *toggle : toggles) {
    if(main_layout->count() != 0) {
      toggleLayout->addWidget(horizontal_line());
    }
    toggleLayout->addWidget(toggle);
  }
}

SelectCar::SelectCar(QWidget* parent): QWidget(parent) {

  QVBoxLayout* main_layout = new QVBoxLayout(this);
  main_layout->setMargin(20);
  main_layout->setSpacing(20);

  // Back button
  QPushButton* back = new QPushButton("Back");
  back->setObjectName("back_btn");
  back->setFixedSize(500, 100);
  connect(back, &QPushButton::clicked, [=]() { emit backPress(); });
  main_layout->addWidget(back, 0, Qt::AlignLeft);

  QListWidget* list = new QListWidget(this);
  list->setStyleSheet("QListView {padding: 40px; background-color: #393939; border-radius: 15px; height: 140px;} QListView::item{height: 100px}");
  //list->setAttribute(Qt::WA_AcceptTouchEvents, true);
  QScroller::grabGesture(list->viewport(), QScroller::LeftMouseButtonGesture);
  list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

  list->addItem("[ Not selected ]");

  QStringList items = get_list("/data/params/d/SupportedCars");
  list->addItems(items);
  list->setCurrentRow(0);

  QString selected = QString::fromStdString(Params().get("SelectedCar"));

  int index = 0;
  for(QString item : items) {
    if(selected == item) {
        list->setCurrentRow(index + 1);
        break;
    }
    index++;
  }

  QObject::connect(list, QOverload<QListWidgetItem*>::of(&QListWidget::itemClicked),
    [=](QListWidgetItem* item){

    if(list->currentRow() == 0)
        Params().remove("SelectedCar");
    else
        Params().put("SelectedCar", list->currentItem()->text().toStdString());

    emit selectedCar();
    });

  main_layout->addWidget(list);
}
