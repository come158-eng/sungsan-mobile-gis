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
     * A preferred Sungsan field-object layer can be supplied. Otherwise the
     * bridge requires one unambiguous Sungsan LandStar import target. The
     * default no-header order is
     * point-name,northing,easting,elevation,code.
     */
    Q_INVOKABLE QVariantMap importLandStarFile( const QString &filePath, QgsProject *project, QgsVectorLayer *preferredLayer = nullptr );

    /**
     * Reads only metadata from a LandStar file without touching project layers.
     *
     * Returns ok/error and parsed project metadata keys:
     * project_region, project_site, project_name, work_date, and method.
     */
    Q_INVOKABLE QVariantMap queryLandStarMetadata( const QString &filePath ) const;

    /**
     * Writes all point features in a layer as
     * point-name,northing,easting,elevation,code.
     * The returned map contains ok, path, count and error values.
     */
    Q_INVOKABLE QVariantMap exportCadText( QgsVectorLayer *layer, const QString &projectHomePath );

  private:
    QgsVectorLayer *selectTargetLayer( QgsProject *project, QgsVectorLayer *preferredLayer, QString *error = nullptr ) const;
};

#endif // SUNGSANSURVEYBRIDGE_H
