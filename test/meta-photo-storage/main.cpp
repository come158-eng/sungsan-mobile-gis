#include <QtCore>
#include <filesystem>
#include <stdexcept>

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
#ifndef Q_OS_WIN
    const QString outsideDir = temp.path() + "/outside";
    QDir().mkpath(outsideDir);
    std::filesystem::create_directory_symlink(outsideDir.toStdString(), (project + "/linked").toStdString());
    require(!FileUtils::storeCameraPhoto(source, project + "/linked/photo.jpg").value("ok").toBool(), "linked outside folder accepted");
#endif
    qInfo("PASS: actual photo-storage functions, encoded file URLs, Korean nested paths, safe retake, diagnostics and project boundary");
    return 0;
  } catch (const std::exception &e) {
    qCritical() << e.what();
    return 1;
  }
}
