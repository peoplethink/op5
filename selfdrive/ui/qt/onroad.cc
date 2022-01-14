#include "selfdrive/ui/qt/onroad.h"

#include <cmath>

#include <QDebug>
#include <QSound>

#include "selfdrive/common/timing.h"
#include "selfdrive/ui/qt/util.h"
#include "selfdrive/common/params.h"
#ifdef ENABLE_MAPS
#include "selfdrive/ui/qt/maps/map.h"
#include "selfdrive/ui/qt/maps/map_helpers.h"
#endif

OnroadWindow::OnroadWindow(QWidget *parent) : QWidget(parent) {
  QVBoxLayout *main_layout  = new QVBoxLayout(this);
  main_layout->setMargin(bdr_s);
  QStackedLayout *stacked_layout = new QStackedLayout;
  stacked_layout->setStackingMode(QStackedLayout::StackAll);
  main_layout->addLayout(stacked_layout);

  QStackedLayout *road_view_layout = new QStackedLayout;
  road_view_layout->setStackingMode(QStackedLayout::StackAll);
  nvg = new NvgWindow(VISION_STREAM_RGB_BACK, this);
  road_view_layout->addWidget(nvg);
  hud = new OnroadHud(this);
  road_view_layout->addWidget(hud);

  nvg->hud = hud;
	
  buttons = new ButtonsWindow(this);
  QObject::connect(uiState(), &UIState::uiUpdate, buttons, &ButtonsWindow::updateState);
  QObject::connect(nvg, &NvgWindow::resizeSignal, [=] (int w) {
    buttons->setFixedWidth(w);
  });
  stacked_layout->addWidget(buttons);


  QWidget * split_wrapper = new QWidget;
  split = new QHBoxLayout(split_wrapper);
  split->setContentsMargins(0, 0, 0, 0);
  split->setSpacing(0);
  split->addLayout(road_view_layout);

  stacked_layout->addWidget(split_wrapper);

  alerts = new OnroadAlerts(this);
  alerts->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  stacked_layout->addWidget(alerts);

  // setup stacking order
  alerts->raise();

  setAttribute(Qt::WA_OpaquePaintEvent);
  QObject::connect(uiState(), &UIState::uiUpdate, this, &OnroadWindow::updateState);
  QObject::connect(uiState(), &UIState::offroadTransition, this, &OnroadWindow::offroadTransition);

#ifdef QCOM2
  // screen recoder - neokii

  record_timer = std::make_shared<QTimer>();
	QObject::connect(record_timer.get(), &QTimer::timeout, [=]() {
    if(recorder) {
      recorder->update_screen();
    }
  });
        record_timer->start(1000/UI_FREQ);
/*
  QWidget* recorder_widget = new QWidget(this);
  QVBoxLayout * recorder_layout = new QVBoxLayout (recorder_widget);
  recorder_layout->setMargin(35);
  recorder = new ScreenRecoder(this);
  recorder_layout->addWidget(recorder);
  recorder_layout->setAlignment(recorder, Qt::AlignRight | Qt::AlignBottom);

  stacked_layout->addWidget(recorder_widget);
  recorder_widget->raise();
  alerts->raise();*/
#endif
}

void OnroadWindow::updateState(const UIState &s) {
  QColor bgColor = bg_colors[s.status];
  Alert alert = Alert::get(*(s.sm), s.scene.started_frame);
  if (s.sm->updated("controlsState") || !alert.equal({})) {
    if (alert.type == "controlsUnresponsive") {
      bgColor = bg_colors[STATUS_ALERT];
    } else if (alert.type == "controlsUnresponsivePermanent") {
      bgColor = bg_colors[STATUS_DISENGAGED];
    }
    if (!s.scene.is_OpenpilotViewEnabled)  
    alerts->updateAlert(alert, bgColor);
  }

  hud->updateState(s);

  if (bg != bgColor) {
    // repaint border
    bg = bgColor;
    update();
  }
}

void OnroadWindow::mouseReleaseEvent(QMouseEvent* e) {

#ifdef QCOM2
  // neokii
  QPoint endPos = e->pos();
  int dx = endPos.x() - startPos.x();
  int dy = endPos.y() - startPos.y();
  if(std::abs(dx) > 250 || std::abs(dy) > 200) {

    if(std::abs(dx) < std::abs(dy)) {

      if(dy < 0) { // upward
        Params().remove("CalibrationParams");
        Params().remove("LiveParameters");
        QTimer::singleShot(1500, []() {
          Params().putBool("SoftRestartTriggered", true);
        });

        QSound::play("../assets/sounds/reset_calibration.wav");
      }
      else { // downward
        QTimer::singleShot(500, []() {
          Params().putBool("SoftRestartTriggered", true);
        });
      }
    }
    else if(std::abs(dx) > std::abs(dy)) {
      if(dx < 0) { // right to left
        if(recorder)
          recorder->toggle();
      }
      else { // left to right
        if(recorder)
          recorder->toggle();
      }
    }

    return;
  }

  if (map != nullptr) {
    bool sidebarVisible = geometry().x() > 0;
    map->setVisible(!sidebarVisible && !map->isVisible());
  }

  // propagation event to parent(HomeWindow)
  QWidget::mouseReleaseEvent(e);
#endif
}

void OnroadWindow::mousePressEvent(QMouseEvent* e) {
#ifdef QCOM2
  startPos = e->pos();
#else
  if (map != nullptr) {
    bool sidebarVisible = geometry().x() > 0;
    map->setVisible(!sidebarVisible && !map->isVisible());
  }

  // propagation event to parent(HomeWindow)
  QWidget::mouseReleaseEvent(e);
#endif
}

void OnroadWindow::offroadTransition(bool offroad) {
#ifdef ENABLE_MAPS
  if (!offroad) {
    if (map == nullptr && (uiState()->has_prime || !MAPBOX_TOKEN.isEmpty())) {
      MapWindow * m = new MapWindow(get_mapbox_settings());
      m->setFixedWidth(topWidget(this)->width() / 2);
      QObject::connect(uiState(), &UIState::offroadTransition, m, &MapWindow::offroadTransition);
      split->addWidget(m, 0, Qt::AlignRight);
      map = m;
    }
  }
#endif

  alerts->updateAlert({}, bg);

  // update stream type
  bool wide_cam = Hardware::TICI() && Params().getBool("EnableWideCamera");
  nvg->setStreamType(wide_cam ? VISION_STREAM_RGB_WIDE : VISION_STREAM_RGB_BACK);

#ifdef QCOM2
  if(offroad && recorder) {
    recorder->stop(false);
  }
#endif
}

void OnroadWindow::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  p.fillRect(rect(), QColor(bg.red(), bg.green(), bg.blue(), 255));
}

// ***** onroad widgets *****

ButtonsWindow::ButtonsWindow(QWidget *parent) : QWidget(parent) {
  QVBoxLayout *main_layout  = new QVBoxLayout(this);

  QWidget *btns_wrapper = new QWidget;
  QHBoxLayout *btns_layout  = new QHBoxLayout(btns_wrapper);
  btns_layout->setSpacing(0);
  btns_layout->setContentsMargins(0, 250, 30, 30);

  main_layout->addWidget(btns_wrapper, 0, Qt::AlignTop);

  // Dynamic lane profile button
  QString initDlpBtn = "";
  dlpBtn = new QPushButton(initDlpBtn);
  QObject::connect(dlpBtn, &QPushButton::clicked, [=]() {
    uiState()->scene.dynamic_lane_profile = uiState()->scene.dynamic_lane_profile + 1;
    if (uiState()->scene.dynamic_lane_profile > 2) {
      uiState()->scene.dynamic_lane_profile = 0;
    }
    if (uiState()->scene.dynamic_lane_profile == 0) {
      Params().put("DynamicLaneProfile", "0", 1);
      dlpBtn->setText("Lane\nonly");
    } else if (uiState()->scene.dynamic_lane_profile == 1) {
      Params().put("DynamicLaneProfile", "1", 1);
      dlpBtn->setText("Lane\nless");
    } else if (uiState()->scene.dynamic_lane_profile == 2) {
      Params().put("DynamicLaneProfile", "2", 1);
      dlpBtn->setText("Auto\nLane");
    }
  });
  dlpBtn->setFixedWidth(187);
  dlpBtn->setFixedHeight(135);
  btns_layout->addWidget(dlpBtn, 0, Qt::AlignLeft);
  btns_layout->addSpacing(0);

  if (uiState()->scene.end_to_end) {
    dlpBtn->hide();
  }

  setStyleSheet(R"(
    QPushButton {
      color: white;
      text-align: center;
      padding: 0px;
      border-width: 6px;
      border-style: solid;
      background-color: rgba(75, 75, 75, 0.3);
    }
  )");
}

void ButtonsWindow::updateState(const UIState &s) {
  if (uiState()->scene.dynamic_lane_profile == 0) {
    dlpBtn->setStyleSheet(QString("font-size: 40px; border-radius: 100px; border-color: %1").arg(dlpBtnColors.at(0)));
    dlpBtn->setText("Lane\nonly");
  } else if (uiState()->scene.dynamic_lane_profile == 1) {
    dlpBtn->setStyleSheet(QString("font-size: 40px; border-radius: 100px; border-color: %1").arg(dlpBtnColors.at(1)));
    dlpBtn->setText("Lane\nless");
  } else if (uiState()->scene.dynamic_lane_profile == 2) {
    dlpBtn->setStyleSheet(QString("font-size: 40px; border-radius: 100px; border-color: %1").arg(dlpBtnColors.at(2)));
    dlpBtn->setText("Auto\nLane");
  }
}

// OnroadAlerts
void OnroadAlerts::updateAlert(const Alert &a, const QColor &color) {
  if (!alert.equal(a) || color != bg) {
    alert = a;
    bg = color;
    update();
  }
}

void OnroadAlerts::paintEvent(QPaintEvent *event) {
  if (alert.size == cereal::ControlsState::AlertSize::NONE) {
    return;
  }
  static std::map<cereal::ControlsState::AlertSize, const int> alert_sizes = {
    {cereal::ControlsState::AlertSize::SMALL, 271},
    {cereal::ControlsState::AlertSize::MID, 420},
    {cereal::ControlsState::AlertSize::FULL, height()},
  };
  int h = alert_sizes[alert.size];
  QRect r = QRect(0, height() - h, width(), h);

  QPainter p(this);

  // draw background + gradient
  p.setPen(Qt::NoPen);
  p.setCompositionMode(QPainter::CompositionMode_SourceOver);

  p.setBrush(QBrush(bg));
  p.drawRect(r);

  QLinearGradient g(0, r.y(), 0, r.bottom());
  g.setColorAt(0, QColor::fromRgbF(0, 0, 0, 0.05));
  g.setColorAt(1, QColor::fromRgbF(0, 0, 0, 0.35));

  p.setCompositionMode(QPainter::CompositionMode_DestinationOver);
  p.setBrush(QBrush(g));
  p.fillRect(r, g);
  p.setCompositionMode(QPainter::CompositionMode_SourceOver);

  // text
  const QPoint c = r.center();
  p.setPen(QColor(0xff, 0xff, 0xff));
  p.setRenderHint(QPainter::TextAntialiasing);
  if (alert.size == cereal::ControlsState::AlertSize::SMALL) {
    configFont(p, "Open Sans", 74, "SemiBold");
    p.drawText(r, Qt::AlignCenter, alert.text1);
  } else if (alert.size == cereal::ControlsState::AlertSize::MID) {
    configFont(p, "Open Sans", 88, "Bold");
    p.drawText(QRect(0, c.y() - 125, width(), 150), Qt::AlignHCenter | Qt::AlignTop, alert.text1);
    configFont(p, "Open Sans", 66, "Regular");
    p.drawText(QRect(0, c.y() + 21, width(), 90), Qt::AlignHCenter, alert.text2);
  } else if (alert.size == cereal::ControlsState::AlertSize::FULL) {
    bool l = alert.text1.length() > 15;
    configFont(p, "Open Sans", l ? 132 : 177, "Bold");
    p.drawText(QRect(0, r.y() + (l ? 240 : 270), width(), 600), Qt::AlignHCenter | Qt::TextWordWrap, alert.text1);
    configFont(p, "Open Sans", 88, "Regular");
    p.drawText(QRect(0, r.height() - (l ? 361 : 420), width(), 300), Qt::AlignHCenter | Qt::TextWordWrap, alert.text2);
  }
}

// OnroadHud
OnroadHud::OnroadHud(QWidget *parent) : QWidget(parent) {
  engage_img = QPixmap("../assets/img_chffr_wheel.png").scaled(img_size, img_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  //dm_img = QPixmap("../assets/img_driver_face.png").scaled(img_size, img_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  connect(this, &OnroadHud::valueChanged, [=] { update(); });
}

void OnroadHud::updateState(const UIState &s) {	
  const SubMaster &sm = *(s.sm);
  const auto cs = sm["controlsState"].getControlsState();
	
  setProperty("status", s.status);
  setProperty("ang_str", s.scene.angleSteers);
	
  // update engageability and DM icons at 2Hz
  if (sm.frame % (UI_FREQ / 2) == 0) {
    setProperty("engageable", cs.getEngageable() || cs.getEnabled());
    //setProperty("dmActive", sm["driverMonitoringState"].getDriverMonitoringState().getIsActiveMode());
  }
  if(uiState()->recording) {
    update();
  }
}

void OnroadHud::paintEvent(QPaintEvent *event) {
  //UIState *s = &QUIState::ui_state;
  QPainter p(this);
  //p.setRenderHint(QPainter::Antialiasing);
	
  // Header gradient
  //QLinearGradient bg(0, header_h - (header_h / 2.5), 0, header_h);
  //bg.setColorAt(0, QColor::fromRgbF(0, 0, 0, 0.45));
  //bg.setColorAt(1, QColor::fromRgbF(0, 0, 0, 0));
  //p.fillRect(0, 0, width(), header_h, bg);
	
  // engage-ability icon
  //if (engageable) {
  if (true) {
    drawIcon(p, rect().right() - radius / 2 - bdr_s * 2, radius / 2 + bdr_s,
             engage_img, bg_colors[status], 5.0, true, ang_str );
  }
}

void NvgWindow::drawText(QPainter &p, int x, int y, const QString &text, int alpha) {
  QFontMetrics fm(p.font());
  QRect init_rect = fm.boundingRect(text);
  QRect real_rect = fm.boundingRect(init_rect, 0, text);
  real_rect.moveCenter({x, y - real_rect.height() / 2});

  p.setPen(QColor(0xff, 0xff, 0xff, alpha));
  p.drawText(real_rect.x(), real_rect.bottom(), text);
}

void NvgWindow::drawTextWithColor(QPainter &p, int x, int y, const QString &text, QColor& color) {
  QFontMetrics fm(p.font());
  QRect init_rect = fm.boundingRect(text);
  QRect real_rect = fm.boundingRect(init_rect, 0, text);
  real_rect.moveCenter({x, y - real_rect.height() / 2});

  p.setPen(color);
  p.drawText(real_rect.x(), real_rect.bottom(), text);
}

void OnroadHud::drawIcon(QPainter &p, int x, int y, QPixmap &img, QBrush bg, float opacity, bool rotation, float angle) {
  // 
  if (rotation) {
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawEllipse(x - radius / 2, y - radius / 2, radius, radius);
    p.setOpacity(opacity);
    p.save();
    p.translate(x, y);
    p.rotate(-angle);
    QRect r = img.rect();
    r.moveCenter(QPoint(0,0));
    p.drawPixmap(r, img);
    p.restore();
  } else {
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawEllipse(x - radius / 2, y - radius / 2, radius, radius);
    p.setOpacity(opacity);
    p.drawPixmap(x - img_size / 2, y - img_size / 2, img);
  }
}
// NvgWindow
void NvgWindow::initializeGL() {
  CameraViewWidget::initializeGL();
  qInfo() << "OpenGL version:" << QString((const char*)glGetString(GL_VERSION));
  qInfo() << "OpenGL vendor:" << QString((const char*)glGetString(GL_VENDOR));
  qInfo() << "OpenGL renderer:" << QString((const char*)glGetString(GL_RENDERER));
  qInfo() << "OpenGL language version:" << QString((const char*)glGetString(GL_SHADING_LANGUAGE_VERSION));

  prev_draw_t = millis_since_boot();
  setBackgroundColor(bg_colors[STATUS_DISENGAGED]);

  //neokii
  ic_brake = QPixmap("../assets/images/img_brake_disc.png");
  //ic_autohold_warning = QPixmap("../assets/images/img_autohold_warning.png").scaled(img_size, img_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  //ic_autohold_active = QPixmap("../assets/images/img_autohold_active.png").scaled(img_size, img_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  ic_nda = QPixmap("../assets/images/img_nda.png");
  ic_hda = QPixmap("../assets/images/img_hda.png");
  ic_tire_pressure = QPixmap("../assets/images/img_tire_pressure.png");
  ic_turn_signal_l = QPixmap("../assets/images/turn_signal_l.png");
  ic_turn_signal_r = QPixmap("../assets/images/turn_signal_r.png");
  ic_satellite = QPixmap("../assets/images/satellite.png");
  ic_bsd_l = QPixmap("../assets/images/img_car_left.png"); //bsd
  ic_bsd_r = QPixmap("../assets/images/img_car_right.png"); //bsd
  ic_lcr = QPixmap("../assets/images/img_lcr.png");
}

void NvgWindow::updateFrameMat(int w, int h) {
  CameraViewWidget::updateFrameMat(w, h);

  UIState *s = uiState();
  s->fb_w = w;
  s->fb_h = h;
  auto intrinsic_matrix = s->wide_camera ? ecam_intrinsic_matrix : fcam_intrinsic_matrix;
  float zoom = ZOOM / intrinsic_matrix.v[0];
  if (s->wide_camera) {
    zoom *= 0.5;
  }
  // Apply transformation such that video pixel coordinates match video
  // 1) Put (0, 0) in the middle of the video
  // 2) Apply same scaling as video
  // 3) Put (0, 0) in top left corner of video
  s->car_space_transform.reset();
  s->car_space_transform.translate(w / 2, h / 2 + y_offset)
      .scale(zoom, zoom)
      .translate(-intrinsic_matrix.v[2], -intrinsic_matrix.v[5]);
}

void NvgWindow::drawLaneLines(QPainter &painter, const UIScene &scene) {
  UIState *s = uiState();
  int steerOverride = (*s->sm)["carState"].getCarState().getSteeringPressed();
  //if (!scene.end_to_end) {
  if (!scene.lateralPlan.dynamicLaneProfileStatus) {
    // lanelines
    for (int i = 0; i < std::size(scene.lane_line_vertices); ++i) {
      if (i == 1 || i == 2) {
        // TODO: can we just use the projected vertices somehow?
        const cereal::ModelDataV2::XYZTData::Reader &line = (*s->sm)["modelV2"].getModelV2().getLaneLines()[i];
        const float default_pos = 1.4;  // when lane poly isn't available
        const float lane_pos = line.getY().size() > 0 ? std::abs(line.getY()[5]) : default_pos;  // get redder when line is closer to car
        float hue = 332.5 * lane_pos - 332.5;  // equivalent to {1.4, 1.0}: {133, 0} (green to red)
        hue = std::fmin(133, fmax(0, hue)) / 360.;  // clip and normalize
        painter.setBrush(QColor::fromHslF(hue, 0.73, 0.64, scene.lane_line_probs[i]));
      } else {
        painter.setBrush(QColor::fromRgbF(1.0, 1.0, 1.0, scene.lane_line_probs[i]));
      }
      painter.drawPolygon(scene.lane_line_vertices[i].v, scene.lane_line_vertices[i].cnt);
    }
    // road edges
    for (int i = 0; i < std::size(scene.road_edge_vertices); ++i) {
      painter.setBrush(QColor::fromRgbF(1.0, 0, 0, std::clamp<float>(1.0 - scene.road_edge_stds[i], 0.0, 1.0)));
      painter.drawPolygon(scene.road_edge_vertices[i].v, scene.road_edge_vertices[i].cnt);
    }
  }
	
  // paint path
  QLinearGradient bg(0, height(), 0, height() / 4);
  if ((*s->sm)["controlsState"].getControlsState().getEnabled()) {
  if (steerOverride) {
      bg.setColorAt(0, redColor(60));
      bg.setColorAt(1, redColor(20));
    } else {
      bg.setColorAt(0, scene.lateralPlan.dynamicLaneProfileStatus ? graceBlueColor() : skyBlueColor());
      bg.setColorAt(1, scene.lateralPlan.dynamicLaneProfileStatus ? graceBlueColor(0) : skyBlueColor(0));
    } 
  } else {
    bg.setColorAt(0, QColor(255, 255, 255));
    bg.setColorAt(1, QColor(255, 255, 255, 0));
  }  
  painter.setBrush(bg);
  painter.drawPolygon(scene.track_vertices.v, scene.track_vertices.cnt);
}

void NvgWindow::drawLead(QPainter &painter, const cereal::ModelDataV2::LeadDataV3::Reader &lead_data, const cereal::RadarState::LeadData::Reader &radar_lead_data, const QPointF &vd, bool cluspeedms, bool is_radar) {
  const float speedBuff = 10.;
  const float leadBuff = 40.;
  const float d_rel = lead_data.getX()[0];
  const float v_rel = lead_data.getV()[0];
  const float radar_d_rel = radar_lead_data.getDRel();
  const float radar_v_abs = (cluspeedms + radar_lead_data.getVRel()) * 3.6;

  float fillAlpha = 0;
  if (d_rel < leadBuff) {
    fillAlpha = 255 * (1.0 - (d_rel / leadBuff));
    if (v_rel < 0) {
      fillAlpha += 255 * (-1 * (v_rel / speedBuff));
    }
    fillAlpha = (int)(fmin(fillAlpha, 255));
  }

  float sz = std::clamp((25 * 30) / (d_rel / 3 + 30), 15.0f, 30.0f) * 2.35;
  float x = std::clamp((float)vd.x(), 0.f, width() - sz / 2);
  float y = std::fmin(height() - sz * .6, (float)vd.y());

  float g_xo = sz / 5;
  float g_yo = sz / 10;
	
  int x_int = (int)x;
  int y_int = (int)y;
	
  QString radar_v_abs_str = QString::number(std::nearbyint(radar_v_abs)) + " km/h";
  QString radar_d_rel_str = QString::number(std::nearbyint(radar_d_rel)) + " m";
  
  QPointF glow[] = {{x + (sz * 1.35) + g_xo, y + sz + g_yo}, {x, y - g_xo}, {x - (sz * 1.35) - g_xo, y + sz + g_yo}};
  painter.setBrush(is_radar ? QColor(86, 121, 216, 255) : QColor(218, 202, 37, 255));
  painter.drawPolygon(glow, std::size(glow));

  // chevron
  QPointF chevron[] = {{x + (sz * 1.25), y + sz}, {x, y}, {x - (sz * 1.25), y + sz}};
  painter.setBrush(redColor(fillAlpha));
  painter.drawPolygon(chevron, std::size(chevron));
	
  painter.setPen(QColor(255, 255, 255, 255));
  configFont(painter, "Open Sans", 55, "Regular");
  painter.drawText(x_int - 100, y_int + 118, radar_v_abs_str);
  painter.setPen(QColor(0, 255, 0, 255));
  configFont(painter, "Open Sans", 55, "Regular");
  painter.drawText(x_int - 72, y_int + 182, radar_d_rel_str);//35, 120
}

void NvgWindow::paintGL() {
  CameraViewWidget::paintGL();
	
  UIState *s = uiState();
  if (s->worldObjectsVisible()) { 
    if(!s->recording) {
      QPainter p(this);
      drawCommunity(p);
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    drawLaneLines(painter, s->scene);
  }

  double cur_draw_t = millis_since_boot();
  double dt = cur_draw_t - prev_draw_t;
  if (dt > 66) {
    // warn on sub 15fps
    LOGW("slow frame time: %.2f", dt);
  }
  prev_draw_t = cur_draw_t;
}

void NvgWindow::showEvent(QShowEvent *event) {
  CameraViewWidget::showEvent(event);

  ui_update_params(uiState());
  prev_draw_t = millis_since_boot();
}


void NvgWindow::drawCommunity(QPainter &p) {

  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(Qt::NoPen);
  p.setOpacity(1.);

  // Header gradient
  QLinearGradient bg(0, header_h - (header_h / 2.5), 0, header_h);
  bg.setColorAt(0, QColor::fromRgbF(0, 0, 0, 0.45));
  bg.setColorAt(1, QColor::fromRgbF(0, 0, 0, 0));
  p.fillRect(0, 0, width(), header_h, bg);

  UIState *s = uiState();

  const SubMaster &sm = *(s->sm);

  auto leads = sm["modelV2"].getModelV2().getLeadsV3();
  auto radar_lead_one = (*s->sm)["radarState"].getRadarState().getLeadOne();
  bool cluspeedms = (*s->sm)["carState"].getCarState().getCluSpeedMs();
  if (leads[0].getProb() > .5) {
    drawLead(p, leads[0], radar_lead_one, s->scene.lead_vertices[0], s->scene.lead_radar[0], cluspeedms);
  }

  drawMaxSpeed(p);
  drawSpeed(p);
  drawSpeedLimit(p);
  drawTurnSignals(p);
  drawGpsStatus(p);
  drawBrake(p);
  drawLcr(p);
	
  if(s->show_tpms && width() > 1200)
    drawTpms(p);
	
  if(s->show_debug && width() > 1200)
    drawDebugText(p);
	
  if(s->show_gear && width() > 1200)
    drawCgear(p);//기어
	
  if(s->show_bsd && width() > 1200)
    drawBsd(p);//bsd

  char str[1024];
  const auto car_state = sm["carState"].getCarState();
  const auto controls_state = sm["controlsState"].getControlsState();
  const auto car_params = sm["carParams"].getCarParams();
  const auto live_params = sm["liveParameters"].getLiveParameters();
  const auto device_state = sm["deviceState"].getDeviceState();
	
  int lateralControlState = controls_state.getLateralControlSelect();
  const char* lateral_state[] = {"PID", "INDI", "LQR"};
	
  auto cpuList = device_state.getCpuTempC();
  float cpuTemp = 0;

  if (cpuList.size() > 0) {
      for(int i = 0; i < cpuList.size(); i++)
          cpuTemp += cpuList[i];
      cpuTemp /= cpuList.size();
  }
	
  const auto scc_smoother = sm["carControl"].getCarControl().getSccSmoother();
  bool is_metric = s->scene.is_metric;
  bool long_control = scc_smoother.getLongControl();

  // kph
  float applyMaxSpeed = scc_smoother.getApplyMaxSpeed();
  float cruiseMaxSpeed = scc_smoother.getCruiseMaxSpeed();

  bool is_cruise_set = (cruiseMaxSpeed > 0 && cruiseMaxSpeed < 255);

  //int mdps_bus = car_params.getMdpsBus();
  int scc_bus = car_params.getSccBus();

  QString infoText;
  infoText.sprintf(" %s SR(%.2f) SC(%.2f) SD(%.2f) (%d) (A%.2f/B%.2f/C%.2f/D%.2f/%.2f)  CPU온도 %.1f°  GENESIS_0813",
		      lateral_state[lateralControlState],
                      //live_params.getAngleOffsetDeg(),
                      //live_params.getAngleOffsetAverageDeg(),
                      controls_state.getSteerRatio(),
                      controls_state.getSteerRateCost(),
                      controls_state.getSteerActuatorDelay(),
                      //mdps_bus, 
		      scc_bus,
                      controls_state.getSccGasFactor(),
                      controls_state.getSccBrakeFactor(),
                      controls_state.getSccCurvatureFactor(),
		      controls_state.getLongitudinalActuatorDelayLowerBound(),
                      controls_state.getLongitudinalActuatorDelayUpperBound(),
	              cpuTemp
                      );

  // info
  configFont(p, "Open Sans", 34, "Regular");
  p.setPen(QColor(0xff, 0xff, 0xff, 0xff));
  p.drawText(rect().left() + 180, rect().height() - 15, infoText);	
  const int h = 60;
  QRect bar_rc(rect().left(), rect().bottom() - h, rect().width(), h);
  p.setBrush(QColor(0, 0, 0, 100));
  p.drawRect(bar_rc);
  drawBottomIcons(p);
}

void NvgWindow::drawMaxSpeed(QPainter &p) {
  UIState *s = uiState();
  const SubMaster &sm = *(s->sm);
  const auto scc_smoother = sm["carControl"].getCarControl().getSccSmoother();
  bool is_metric = s->scene.is_metric;
  bool long_control = scc_smoother.getLongControl();

  // kph
  float applyMaxSpeed = scc_smoother.getApplyMaxSpeed();
  float cruiseMaxSpeed = scc_smoother.getCruiseMaxSpeed();
  bool is_cruise_set = (cruiseMaxSpeed > 0 && cruiseMaxSpeed < 255);

  QRect rc(30, 30, 184, 202);
  p.setPen(QPen(QColor(0xff, 0xff, 0xff, 100), 10));
  p.setBrush(QColor(0, 0, 0, 100));
  p.drawRoundedRect(rc, 20, 20);
  p.setPen(Qt::NoPen);

  if (is_cruise_set) {
    char str[256];
    if (is_metric)
        snprintf(str, sizeof(str), "%d", (int)(applyMaxSpeed + 0.5));
    else
        snprintf(str, sizeof(str), "%d", (int)(applyMaxSpeed*KM_TO_MILE + 0.5));

    configFont(p, "Open Sans", 45, "Bold");
    drawText(p, rc.center().x(), 100, str, 255);

    if (is_metric)
        snprintf(str, sizeof(str), "%d", (int)(cruiseMaxSpeed + 0.5));
    else
        snprintf(str, sizeof(str), "%d", (int)(cruiseMaxSpeed*KM_TO_MILE + 0.5));

    configFont(p, "Open Sans", 76, "Bold");
    drawText(p, rc.center().x(), 195, str, 255);
  } else {
    if(long_control) {
      configFont(p, "Open Sans", 48, "sans-semibold");
      drawText(p, rc.center().x(), 100, "OP", 100);
    }
    else {
      configFont(p, "Open Sans", 48, "sans-semibold");
      drawText(p, rc.center().x(), 100, "MAX", 100);
    }

    configFont(p, "Open Sans", 76, "sans-semibold");
    drawText(p, rc.center().x(), 195, "N/A", 100);
  }
}

void NvgWindow::drawSpeed(QPainter &p) {
  UIState *s = uiState();
  const SubMaster &sm = *(s->sm);
  float cur_speed = std::max(0.0, sm["carState"].getCarState().getCluSpeedMs() * (s->scene.is_metric ? MS_TO_KPH : MS_TO_MPH));

  QString speed;
  speed.sprintf("%.0f", cur_speed);
  configFont(p, "Open Sans", 176, "Bold");
  drawText(p, rect().center().x(), 230, speed);
  configFont(p, "Open Sans", 66, "Regular");
  //drawText(p, rect().center().x(), 310, s->scene.is_metric ? "km/h" : "mph", 200)
}

static const QColor get_tpms_color(float tpms) {
    if(tpms < 5 || tpms > 60) // N/A
        return QColor(255, 255, 255, 220);
    if(tpms < 31)
        return QColor(255, 90, 90, 220);
    return QColor(255, 255, 255, 220);
}

static const QString get_tpms_text(float tpms) {
    if(tpms < 5 || tpms > 60)
        return "";

    char str[32];
    snprintf(str, sizeof(str), "%.0f", round(tpms));
    return QString(str);
}

void NvgWindow::drawText2(QPainter &p, int x, int y, int flags, const QString &text, const QColor& color) {
  QFontMetrics fm(p.font());
  QRect rect = fm.boundingRect(text);
  p.setPen(color);
  p.drawText(QRect(x, y, rect.width(), rect.height()), flags, text);
}

void NvgWindow::drawBottomIcons(QPainter &p) {
  const SubMaster &sm = *(uiState()->sm);
  auto car_state = sm["carState"].getCarState();
  auto scc_smoother = sm["carControl"].getCarControl().getSccSmoother();

  int x = radius / 2 + (bdr_s * 2) + (radius + 50);
  const int y = rect().bottom() - footer_h / 2 - 10;

  // cruise gap
  int gap = car_state.getCruiseGap();
  bool longControl = scc_smoother.getLongControl();
  int autoTrGap = scc_smoother.getAutoTrGap();

  p.setPen(Qt::NoPen);
  p.setBrush(QBrush(QColor(255, 255, 255, 255 * 0.0f)));
  p.drawEllipse(x - radius / 2, y - radius / 2, radius, radius);

  QString str;
  float textSize = 50.f;
  QColor textColor = QColor(255, 255, 255, 200);

  if(gap <= 0) {
    str = "N/A";
  }
  else if(longControl && gap == autoTrGap) {
    str = "AUTO";
    textColor = QColor(255, 255, 255, 250);
  }
  else {
    str.sprintf("%d", (int)gap);
    textColor = QColor(120, 255, 120, 200);
    textSize = 70.f;
  }

  configFont(p, "Open Sans", 35, "Bold");
  drawText(p, x, y-20, "", 200);

  configFont(p, "Open Sans", textSize, "Bold");
  drawTextWithColor(p, x-290, y+140, str, textColor);
/*	
  // brake
  int w = 1600;
  int h = 30;
  int x = (width() + (bdr_s*2))/2 - w/2 - bdr_s;
  int y = 40 - bdr_s + 30;

  bool brake_valid = car_state.getBrakeLights();
  float img_alpha = brake_valid ? 1.0f : 0.15f;
  float bg_alpha = brake_valid ? 0.0f : 0.0f;
  drawIcon(p, w, h, x, y, ic_brake, QColor(0, 0, 0, (255 * bg_alpha)), img_alpha);

  // auto hold
  int autohold = car_state.getAutoHold();
  if(autohold >= 0) {
    x = radius / 2 + (bdr_s * 2) + (radius + 50) * 3;
    img_alpha = autohold > 0 ? 1.0f : 0.15f;
    bg_alpha = autohold > 0 ? 0.0f : 0.0f;
    drawIcon(p, x, y, autohold > 1 ? ic_autohold_warning : ic_autohold_active,
            QColor(0, 0, 0, (255 * bg_alpha)), img_alpha);
  }
*/	
  p.setOpacity(1.);
}

void NvgWindow::drawBrake(QPainter &p) {
  const SubMaster &sm = *(uiState()->sm);
  auto car_state = sm["carState"].getCarState();
  bool brake_valid = car_state.getBrakeLights();
	
  int w = 1500;
  int h = 30;
  int x = (width() + (bdr_s*2))/2 - w/2 - bdr_s;
  int y = 40 - bdr_s + 30;
  
  if (brake_valid) {
    p.drawPixmap(x, y, w, h, ic_brake);
    p.setOpacity(1.f);
  }
}

void NvgWindow::drawLcr(QPainter &p) {
  const SubMaster &sm = *(uiState()->sm);
  auto controls_state = sm["controlsState"].getControlsState().getEnabled();
  auto car_state = sm["carState"].getCarState().getCluSpeedMs();

  const int w = 120;
  const int h = 120;
  const int x = width() - w - 60;
  const int y = 620;
	
  if (sm["controlsState"].getControlsState().getEnabled() && (sm["carState"].getCarState().getCluSpeedMs()) >= 16.111111111) {
    p.setOpacity(1.f);
    p.drawPixmap(x, y, w, h, ic_lcr);
  }
}
	  
void NvgWindow::drawTpms(QPainter &p) {
  const SubMaster &sm = *(uiState()->sm);
  auto car_state = sm["carState"].getCarState();

  const int w = 58;
  const int h = 126;
  const int x = 110 + 1625;
  const int y = height() - h - 80;

  auto tpms = car_state.getTpms();
  const float fl = tpms.getFl();
  const float fr = tpms.getFr();
  const float rl = tpms.getRl();
  const float rr = tpms.getRr();

  p.setOpacity(0.8);
  p.drawPixmap(x, y, w, h, ic_tire_pressure);

  configFont(p, "Open Sans", 38, "Bold");

  QFontMetrics fm(p.font());
  QRect rcFont = fm.boundingRect("9");

  int center_x = x + 4;
  int center_y = y + h/2;
  const int marginX = (int)(rcFont.width() * 2.7f);
  const int marginY = (int)((h/2 - rcFont.height()) * 0.7f);

  drawText2(p, center_x-marginX, center_y-marginY-rcFont.height(), Qt::AlignRight, get_tpms_text(fl), get_tpms_color(fl));
  drawText2(p, center_x+marginX, center_y-marginY-rcFont.height(), Qt::AlignLeft, get_tpms_text(fr), get_tpms_color(fr));
  drawText2(p, center_x-marginX, center_y+marginY, Qt::AlignRight, get_tpms_text(rl), get_tpms_color(rl));
  drawText2(p, center_x+marginX, center_y+marginY, Qt::AlignLeft, get_tpms_text(rr), get_tpms_color(rr));
}

void NvgWindow::drawSpeedLimit(QPainter &p) {
  const SubMaster &sm = *(uiState()->sm);
  auto car_state = sm["carState"].getCarState();
  auto scc_smoother = sm["carControl"].getCarControl().getSccSmoother();

  int activeNDA = scc_smoother.getRoadLimitSpeedActive();
  int limit_speed = scc_smoother.getRoadLimitSpeed();
  int left_dist = scc_smoother.getRoadLimitSpeedLeftDist();
  //activeNDA = 1; //

  if(activeNDA > 0)
  {
      int w = 180;
      int h = 40;
      int x = (width() + (bdr_s*2))/2 - w/2 - bdr_s;
      int y = 275 - bdr_s;

      p.setOpacity(1.f);
      p.drawPixmap(x, y, w, h, activeNDA == 1 ? ic_nda : ic_hda);
  }

  if(limit_speed > 10 && left_dist > 0)
  {
    int radius = 192;

    int x = 1655;
    int y = 255;

    p.setPen(Qt::NoPen);
    p.setBrush(QBrush(QColor(255, 127, 080, 255)));
    QRect rect = QRect(x, y, radius, radius);
    p.drawEllipse(rect);

    p.setBrush(QBrush(QColor(255, 255, 255, 255)));

    const int tickness = 18;
    rect.adjust(tickness, tickness, -tickness, -tickness);
    p.drawEllipse(rect);

    QString str_limit_speed, str_left_dist;
    str_limit_speed.sprintf("%d", limit_speed);

    if(left_dist >= 1000)
      str_left_dist.sprintf("%.1fkm", left_dist / 1000.f);
    else
      str_left_dist.sprintf("%dm", left_dist);

    configFont(p, "Open Sans", 80, "Bold");
    p.setPen(QColor(0, 0, 0, 230));
    p.drawText(rect, Qt::AlignCenter, str_limit_speed);

    configFont(p, "Open Sans", 60, "Bold");
    rect.translate(0, radius/2 + 45);
    rect.adjust(-30, 0, 30, 0);
    p.setPen(QColor(255, 255, 255, 230));
    p.drawText(rect, Qt::AlignCenter, str_left_dist);
  }
  else {
    auto controls_state = sm["controlsState"].getControlsState();
    int sccStockCamAct = (int)controls_state.getSccStockCamAct();
    int sccStockCamStatus = (int)controls_state.getSccStockCamStatus();

    if(sccStockCamAct == 2 && sccStockCamStatus == 2) {
      int radius = 192;

      int x = 30;
      int y = 270;

      p.setPen(Qt::NoPen);

      p.setBrush(QBrush(QColor(255, 0, 0, 255)));
      QRect rect = QRect(x, y, radius, radius);
      p.drawEllipse(rect);

      p.setBrush(QBrush(QColor(255, 255, 255, 255)));

      const int tickness = 14;
      rect.adjust(tickness, tickness, -tickness, -tickness);
      p.drawEllipse(rect);

      configFont(p, "Open Sans", 70, "Bold");
      p.setPen(QColor(0, 0, 0, 230));
      p.drawText(rect, Qt::AlignCenter, "CAM");
    }
  }
}

void NvgWindow::drawTurnSignals(QPainter &p) {
  static int blink_index = 0;
  static int blink_wait = 0;
  static double prev_ts = 0.0;

  if(blink_wait > 0) {
    blink_wait--;
    blink_index = 0;
  }
  else {
    const SubMaster &sm = *(uiState()->sm);
    auto car_state = sm["carState"].getCarState();
    bool left_on = car_state.getLeftBlinker();
    bool right_on = car_state.getRightBlinker();

    const float img_alpha = 0.8f;
    const int fb_w = width() / 2 - 200;
    const int center_x = width() / 2;
    const int w = fb_w / 25;
    const int h = 170;
    const int gap = fb_w / 25;
    const int margin = (int)(fb_w / 3.8f);
    const int base_y = (height() - h) / 2 - 360;
    const int draw_count = 7;

    int x = center_x;
    int y = base_y;

    if(left_on) {
      for(int i = 0; i < draw_count; i++) {
        float alpha = img_alpha;
        int d = std::abs(blink_index - i);
        if(d > 0)
          alpha /= d*2;

        p.setOpacity(alpha);
        float factor = (float)draw_count / (i + draw_count);
        p.drawPixmap(x - w - margin, y + (h-h*factor)/2, w*factor, h*factor, ic_turn_signal_l);
        x -= gap + w;
      }
    }

    x = center_x;
    if(right_on) {
      for(int i = 0; i < draw_count; i++) {
        float alpha = img_alpha;
        int d = std::abs(blink_index - i);
        if(d > 0)
          alpha /= d*2;

        float factor = (float)draw_count / (i + draw_count);
        p.setOpacity(alpha);
        p.drawPixmap(x + margin, y + (h-h*factor)/2, w*factor, h*factor, ic_turn_signal_r);
        x += gap + w;
      }
    }

    if(left_on || right_on) {

      double now = millis_since_boot();
      if(now - prev_ts > 900/UI_FREQ) {
        prev_ts = now;
        blink_index++;
      }

      if(blink_index >= draw_count) {
        blink_index = draw_count - 1;
        blink_wait = UI_FREQ/4;
      }
    }
    else {
      blink_index = 0;
    }
  }

  p.setOpacity(1.);
}

void NvgWindow::drawGpsStatus(QPainter &p) {
  const SubMaster &sm = *(uiState()->sm);
  auto gps = sm["gpsLocationExternal"].getGpsLocationExternal();
  float accuracy = gps.getAccuracy();
  if(accuracy < 0.01f || accuracy > 20.f)
    return;

  int w = 85;
  int h = 65;
  int x = width() - w - 290;
  int y = 30;

  p.setOpacity(0.8);
  p.drawPixmap(x, y, w, h, ic_satellite);

  configFont(p, "Open Sans", 40, "Bold");
  p.setPen(QColor(255, 255, 255, 200));
  p.setRenderHint(QPainter::TextAntialiasing);

  QRect rect = QRect(x, y + h + 10, w, 40);
  rect.adjust(-30, 0, 30, 0);

  QString str;
  str.sprintf("%.1fm", accuracy);
  p.drawText(rect, Qt::AlignHCenter, str);
  p.setOpacity(1.);
}

void NvgWindow::drawDebugText(QPainter &p) {
  const SubMaster &sm = *(uiState()->sm);
  QString str, temp;

  int y = 80;
  const int height = 60;

  const int text_x = width()/2 + 250;

  auto controls_state = sm["controlsState"].getControlsState();
  auto car_control = sm["carControl"].getCarControl();
  auto car_state = sm["carState"].getCarState();

  float applyAccel = controls_state.getApplyAccel();

  float aReqValue = controls_state.getAReqValue();
  float aReqValueMin = controls_state.getAReqValueMin();
  float aReqValueMax = controls_state.getAReqValueMax();

  int sccStockCamAct = (int)controls_state.getSccStockCamAct();
  int sccStockCamStatus = (int)controls_state.getSccStockCamStatus();

  int longControlState = (int)controls_state.getLongControlState();
  float vPid = controls_state.getVPid();
  float upAccelCmd = controls_state.getUpAccelCmd();
  float uiAccelCmd = controls_state.getUiAccelCmd();
  float ufAccelCmd = controls_state.getUfAccelCmd();
  float accel = car_control.getActuators().getAccel();

  const char* long_state[] = {"off", "pid", "stopping", "starting"};

  configFont(p, "Open Sans", 35, "Regular");
  p.setPen(QColor(255, 255, 255, 200));
  p.setRenderHint(QPainter::TextAntialiasing);

  str.sprintf("State: %s\n", long_state[longControlState]);
  p.drawText(text_x, y, str);

  y += height;
  str.sprintf("vPid: %.3f(%.1f)\n", vPid, vPid * 3.6f);
  p.drawText(text_x, y, str);

  y += height;
  str.sprintf("P: %.3f\n", upAccelCmd);
  p.drawText(text_x, y, str);

  y += height;
  str.sprintf("I: %.3f\n", uiAccelCmd);
  p.drawText(text_x, y, str);

  y += height;
  str.sprintf("F: %.3f\n", ufAccelCmd);
  p.drawText(text_x, y, str);

  y += height;
  str.sprintf("Accel: %.3f\n", accel);
  p.drawText(text_x, y, str);

  y += height;
  str.sprintf("Apply: %.3f, Stock: %.3f\n", applyAccel, aReqValue);
  p.drawText(text_x, y, str);

  y += height;
  str.sprintf("%.3f (%.3f/%.3f)\n", aReqValue, aReqValueMin, aReqValueMax);
  p.drawText(text_x, y, str);

  auto lead_radar = sm["radarState"].getRadarState().getLeadOne();
  auto lead_one = sm["modelV2"].getModelV2().getLeadsV3()[0];

  float radar_dist = lead_radar.getStatus() && lead_radar.getRadar() ? lead_radar.getDRel() : 0;
  float vision_dist = lead_one.getProb() > .5 ? (lead_one.getX()[0] - 1.5) : 0;

  y += height;
  str.sprintf("Lead: %.1f/%.1f/%.1f\n", radar_dist, vision_dist, (radar_dist - vision_dist));
  p.drawText(text_x, y, str);
}
//기어
void NvgWindow::drawCgear(QPainter &p) {
  const SubMaster &sm = *(uiState()->sm);
  auto car_state = sm["carState"].getCarState();

  auto t_gear = car_state.getCurrentGear();
  int shifter;

  shifter = int(car_state.getGearShifter());

  QString tgear, tgearshifter;

  tgear.sprintf("%.0f", t_gear);
  configFont(p, "Open Sans", 150, "Bold");

  //shifter = 1; //디버그용
  p.setPen(QColor(255, 255, 255, 255)); 

  int x_gear = 45;
  int y_gear = 952;
  if ((t_gear < 9) && (t_gear !=0)) { 
    p.drawText(x_gear, y_gear, tgear);
  } else if (t_gear == 14 ) { 
    p.setPen(QColor(201, 34, 49, 255));
    p.drawText(x_gear, y_gear, "R");
  } else if (shifter == 1 ) { 
    p.setPen(QColor(255, 255, 255, 255));
    p.drawText(x_gear, y_gear, "P");
  } else if (shifter == 3 ) {  
    p.setPen(QColor(255, 255, 255, 255));
    p.drawText(x_gear, y_gear, "N");
  }
  // 1 "P"   2 "D"  3 "N" 4 "R"

}

void NvgWindow::drawBsd(QPainter &p) {
  const SubMaster &sm = *(uiState()->sm);
  auto car_state = sm["carState"].getCarState();

  const int car_size = 230;
  const int car_x_left =  380;
  const int car_x_right = 1500;
  const int car_y = 580;
  const int car_img_size_w = (car_size * 1);
  const int car_img_size_h = (car_size * 1);
  const int car_img_x_left = (car_x_left - (car_img_size_w / 2));
  const int car_img_x_right = (car_x_right - (car_img_size_w / 2));
  const int car_img_y = (car_y - (car_size / 4));


  bool leftblindspot;
  bool rightblindspot;
  int blindspot_blinkingrate = 120;
  int car_valid_status_changed = 0;
  int car_valid_status = 0;

  bool car_valid_left = bool(car_state.getLeftBlindspot());
  bool car_valid_right = bool(car_state.getRightBlindspot());

  //car_valid_left = 1; // 디버그용
  //car_valid_right = 1;

    if (car_valid_status_changed != car_valid_status) {
      blindspot_blinkingrate = 114;
      car_valid_status_changed = car_valid_status;
    }
    if (car_valid_left || car_valid_right) {
      if (!car_valid_left && car_valid_right) {
        car_valid_status = 1;
      } else if (car_valid_left && !car_valid_right) {
        car_valid_status = 2;
      } else if (car_valid_left && car_valid_right) {
        car_valid_status = 3;
      } else {
        car_valid_status = 0;
      }
      //blindspot_blinkingrate -= 6;
      if(blindspot_blinkingrate<0) blindspot_blinkingrate = 120;
      if (blindspot_blinkingrate>=60) {
        p.setOpacity(1.0);
      } else {
        p.setOpacity(0.0);;
      }
    } else {
      blindspot_blinkingrate = 120;
    }

    if(car_valid_left) {
      p.drawPixmap(car_img_x_left, car_img_y, car_img_size_w, car_img_size_h, ic_bsd_l);
    }
    if(car_valid_right) {
      p.drawPixmap(car_img_x_right, car_img_y, car_img_size_w, car_img_size_h, ic_bsd_r);
    }

}
