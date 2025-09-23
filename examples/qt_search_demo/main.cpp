#include <QApplication>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

#include <pulse/pulse.hpp>
#include <pulse/adapters/qt.hpp>

#include <chrono>
#include <string>

using namespace pulse;
using namespace std::chrono_literals;

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  // ----- UI -----
  QWidget window;
  window.setWindowTitle("Pulse × Qt | Search Demo");

  auto* input  = new QLineEdit;
  auto* status = new QLabel("type at least 2 chars…");
  auto* ticks  = new QLabel("ticks: 0");

  auto* layout = new QVBoxLayout;
  layout->addWidget(new QLabel("Search:"));
  layout->addWidget(input);
  layout->addSpacing(8);
  layout->addWidget(status);
  layout->addSpacing(8);
  layout->addWidget(ticks);
  window.setLayout(layout);
  window.resize(360, 160);
  window.show();

  // ----- Reactive: input -> debounce -> delivery to UI -----
  // 1) Qt signal -> observable<QString>
  auto texts = pulse::qt::from_signal1(input, &QLineEdit::textChanged);

  // 2) lvalue executor for debounce
  inline_executor ui;

  // 3) pipeline: trim + len>=2 + debounce + observe_on(Qt)
  auto sub_search = (texts
    | map([](const QString& s){ return s.trimmed().toStdString(); })
    | filter([](const std::string& s){ return s.size() >= 2; })
    | debounce(250ms, ui)
    | pulse::qt::observe_on(input) // repost to the UI thread via QMetaObject::invokeMethod
  ).subscribe([=](const std::string& q){
    status->setText(QString::fromStdString("[query] " + q));
  });

  // ----- Ticks on QTimer without locks -----
  int counter = 0;
  auto sub_ticks = pulse::qt::qt_interval(500ms, &window).subscribe([=, &counter](int){
    ++counter;
    ticks->setText(QString("ticks: %1").arg(counter));
  });

  // Cleaning up on window closing is not required - subscription RAII + QObject::destroyed
  return app.exec();
}
