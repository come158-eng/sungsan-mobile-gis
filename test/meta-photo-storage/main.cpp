#include <QtCore>
#include <filesystem>
#include <stdexcept>
#include <limits>

// The production functions below need only the active project's filename.
class QgsProject {
  public:
    QString path;
    QString fileName() const { return path; }
    static QgsProject *instance() { static QgsProject project; return &project; }
};
namespace Qgis { enum class MessageLevel { Warning }; }
class QgsMessageLog {
  public:
    static void logMessage(const QString &, const QString &, Qgis::MessageLevel) {}
};
class FileUtils {
  public:
    static bool isWithinProjectDirectory(const QString &);
    static bool replaceFileSafely(const QString &, const QString &);
    static QVariantMap storeCameraPhoto(const QString &, const QString &);
};
#include "production-fileutils.inc"

using QgsFeatureId = qint64;
#define FID_NULL std::numeric_limits<QgsFeatureId>::min()
#define FID_IS_NULL(fid) ((fid) == FID_NULL)
#define FID_IS_NEW(fid) ((fid) < 0 && (fid) != FID_NULL)
struct TestFields {
  int lookupField(const QString &name) const { return name == "surveyed_at" ? 0 : -1; }
};
struct TestLayer {
  bool fieldObjects = true;
  QVariant customProperty(const QString &, bool) const { return fieldObjects; }
  TestFields fields() const { return {}; }
};
struct TestFeature {
  QgsFeatureId featureId = FID_NULL;
  QgsFeatureId id() const { return featureId; }
};
class FeatureModel {
  public:
    enum { AttributeValue = 1 };
    TestLayer *mLayer = nullptr;
    TestFeature mFeature;
    QVariant value;
    int index(int i) const { return i; }
    bool setData(int, const QVariant &v, int) { value = v; return true; }
    void stampFieldSurveyTime();
};
#include "production-fieldtime.inc"

void require(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
void write(const QString &path, const QByteArray &bytes) {
  QFile file(path);
  require(file.open(QIODevice::WriteOnly), "write open failed");
  require(file.write(bytes) == bytes.size(), "write failed");
}
QByteArray read(const QString &path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly), "read failed");
  return file.readAll();
}
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  try {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory failed");
    const QString project = temp.path() + "/project";
    QDir().mkpath(project);
    QgsProject::instance()->path = project + "/field.qgs";
    write(QgsProject::instance()->path, "<qgis/>");
    const QString source = temp.path() + QString::fromUtf8("/촬영 원본.jpg");
    const QString destination = project + QString::fromUtf8("/images/현장객체/5555 (1).jpg");
    const QByteArray first(128000, 'A');
    write(source, first);
    auto result = FileUtils::storeCameraPhoto(QUrl::fromLocalFile(source).toString(QUrl::FullyEncoded),
                                             QUrl::fromLocalFile(destination).toString(QUrl::FullyEncoded));
    require(result.value("ok").toBool(), "file URL / Korean filename save failed");
    require(read(destination) == first && !QFile::exists(source), "saved bytes or temporary cleanup wrong");
    result = FileUtils::storeCameraPhoto(source, destination);
    require(!result.value("ok").toBool() && !result.value("error").toString().isEmpty(), "missing capture not diagnosed");
    require(read(destination) == first, "failed retake changed previous photo");
    require(FileUtils::replaceFileSafely(destination, destination), "same-path image removed");
    write(source, "new-photo");
    require(FileUtils::replaceFileSafely(source, destination), "atomic retake failed");
    require(read(destination) == "new-photo", "retake bytes wrong");
    write(source, "protected-capture");
    const QString outside = temp.path() + "/project-other.jpg";
    require(!FileUtils::storeCameraPhoto(source, outside).value("ok").toBool(), "outside project accepted");
    require(QFile::exists(source) && !QFile::exists(outside), "failed copy changed capture or outside file");
    require(!FileUtils::storeCameraPhoto(source, project + "/../escape.jpg").value("ok").toBool(), "parent escape accepted");
    TestLayer layer;
    FeatureModel model;
    model.mLayer = &layer;
    const QDateTime oldTime = QDateTime::fromMSecsSinceEpoch(1000);
    const QDateTime before = QDateTime::currentDateTime();
    model.value = oldTime;
    model.stampFieldSurveyTime();
    require(model.value.toDateTime() >= before, "unsaved FID_NULL feature kept remembered survey time");
    model.mFeature.featureId = -1;
    model.value = oldTime;
    model.stampFieldSurveyTime();
    require(model.value.toDateTime() >= before, "temporary new feature kept remembered survey time");
    model.mFeature.featureId = 42;
    model.value = oldTime;
    model.stampFieldSurveyTime();
    require(model.value.toDateTime() == oldTime, "editing an existing feature changed survey time");
    model.mFeature.featureId = FID_NULL;
    layer.fieldObjects = false;
    model.stampFieldSurveyTime();
    require(model.value.toDateTime() == oldTime, "unrelated layer survey time changed");
#ifndef Q_OS_WIN
    const QString outsideDir = temp.path() + "/outside";
    QDir().mkpath(outsideDir);
    std::filesystem::create_directory_symlink(outsideDir.toStdString(), (project + "/linked").toStdString());
    require(!FileUtils::storeCameraPhoto(source, project + "/linked/photo.jpg").value("ok").toBool(), "linked outside folder accepted");
#endif
    qInfo("PASS: actual photo storage and timestamp functions, URLs, safe retake, project boundary, unsaved/new capture times and existing-time preservation");
    return 0;
  } catch (const std::exception &e) {
    qCritical() << e.what();
    return 1;
  }
}
