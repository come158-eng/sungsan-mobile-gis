/***************************************************************************
  sungsansurveybridge.h
  ---------------------
  LandStar/CAD point exchange for Sungsan Mobile GIS.
 ***************************************************************************/

#ifndef SUNGSANSURVEYBRIDGE_H
#define SUNGSANSURVEYBRIDGE_H

#include "qfield_core_export.h"

#include <QObject>
#include <QVariantMap>

class QgsProject;
class QgsVectorLayer;

/**
 * Imports LandStar delimited point files into a project point layer and
 * creates the compact text format used by the Sungsan CAD workflow.
 *
 * The bridge deliberately accepts ordinary files only. Android content URIs
 * are copied into the application's private LandStar inbox by the platform
 * layer before this class sees them.
 */
class QFIELD_CORE_EXPORT SungsanSurveyBridge : public QObject
{
    Q_OBJECT

  public:
    explicit SungsanSurveyBridge( QObject *parent = nullptr );

    /**
     * Imports one UTF-8 LandStar CSV/TXT/PXY/KOF file.
     *
     * A preferred point layer can be supplied. Otherwise the bridge selects
     * the Sungsan field-object layer and finally falls back to the first
     * writable point layer. The default no-header order is
     * point-name,northing,easting,elevation,code.
     */
    Q_INVOKABLE QVariantMap importLandStarFile( const QString &filePath, QgsProject *project, QgsVectorLayer *preferredLayer = nullptr );

    /**
     * Writes all point features in a layer as
     * point-name,northing,easting,elevation,code.
     * The returned map contains ok, path, count and error values.
     */
    Q_INVOKABLE QVariantMap exportCadText( QgsVectorLayer *layer, const QString &projectHomePath );

  private:
    QgsVectorLayer *selectTargetLayer( QgsProject *project, QgsVectorLayer *preferredLayer ) const;
};

#endif // SUNGSANSURVEYBRIDGE_H
