#include <QtQuickTest/quicktest.h>

#include <QFile>
#include <QTemporaryDir>
#include <QDir>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QImage>
#include <QtTest/QTest>

#include <cstdlib>

class Setup : public QObject {
  Q_OBJECT
public:
  Q_INVOKABLE bool transparentMarks() {
    for (const auto *path : {AMT_BRAND_IMAGE, AMT_BRAND_LIGHT_IMAGE}) {
      const QImage mark(QString::fromUtf8(path));
      if (mark.isNull() || !mark.hasAlphaChannel() || mark.pixelColor(0,0).alpha() != 0
          || mark.pixelColor(mark.width()/2,mark.height()/2).alpha() != 0) return false;
    }
    return true;
  }
  Q_INVOKABLE void pressBack(QQuickWindow *window) { if (window) QTest::keyClick(window, Qt::Key_Back); }
  Q_INVOKABLE bool capture(QQuickWindow *window, const QString &name) {
    const auto directory = qEnvironmentVariable("SYNSEMBLE_CAPTURE_DIR");
    return window && !directory.isEmpty() && window->grabWindow().save(QDir(directory).filePath(name + ".png"));
  }
  Q_INVOKABLE bool render(QQuickWindow *window) { return window && !window->grabWindow().isNull(); }
public slots:
  void qmlEngineAvailable(QQmlEngine *engine) {
    engine->rootContext()->setContextProperty("captureDirectory", qEnvironmentVariable("SYNSEMBLE_CAPTURE_DIR"));
    engine->rootContext()->setContextProperty("screenCapture", this);
    engine->rootContext()->setContextProperty("baselineMode", qEnvironmentVariableIsSet("SYNSEMBLE_QML_BASELINE_DIR"));
  }
};

int main(int argc, char **argv) {
  QTEST_SET_MAIN_SOURCE_PATH

  QTemporaryDir testDirectory;
  if (!testDirectory.isValid()) {
    return EXIT_FAILURE;
  }

  const QString testPath = testDirectory.filePath("tst_transcript_scroll.qml");
  const QString helperPath = testDirectory.filePath("TranscriptScroll.js");
  if ((!qEnvironmentVariableIsSet("SYNSEMBLE_MEASURE_QML") && !QFile::copy(QStringLiteral(AMT_QML_TEST_SOURCE), testPath)) ||
      !QFile::copy(QStringLiteral(AMT_TRANSCRIPT_SCROLL_SOURCE), helperPath)) {
    return EXIT_FAILURE;
  }

  const QByteArray sourceDirectory = QFile::encodeName(testDirectory.path());
  QDir().mkpath(testDirectory.filePath("app"));
  const QDir qmlDirectory(qEnvironmentVariable("SYNSEMBLE_QML_BASELINE_DIR", QStringLiteral(AMT_QML_DIRECTORY)));
  for (const auto &name : qmlDirectory.entryList({"*.qml", "*.js"}, QDir::Files)) {
    if (!QFile::copy(qmlDirectory.filePath(name), testDirectory.filePath("app/" + name))) return EXIT_FAILURE;
  }
  if (qEnvironmentVariableIsSet("SYNSEMBLE_MEASURE_QML")) {
    if (!QFile::copy(QStringLiteral(AMT_PERFORMANCE_TEST_SOURCE), testDirectory.filePath("tst_performance.qml"))) return EXIT_FAILURE;
  } else if (!QFile::copy(QStringLiteral(AMT_MOBILE_TEST_SOURCE), testDirectory.filePath("tst_mobile.qml"))) return EXIT_FAILURE;
  if (!QFile::copy(QStringLiteral(AMT_BRAND_IMAGE), testDirectory.filePath("synsemble.png"))) return EXIT_FAILURE;
  if (!QFile::copy(QStringLiteral(AMT_BRAND_LIGHT_IMAGE), testDirectory.filePath("synsemble-light.png"))) return EXIT_FAILURE;
  Setup setup;
  return quick_test_main_with_setup(argc, argv, "amt_qml", sourceDirectory.constData(), &setup);
}

#include "test_qml.moc"
