/***************************************************************************
  cameraorientationnormalizer.cpp - CameraOrientationNormalizer

 ---------------------
 begin                : 16.4.2026
 copyright            : (C) 2026 by Kaustuv Pokharel
 email                : kaustuv@opengis.ch
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "cameraorientationnormalizer.h"

#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QSaveFile>
#include <QScreen>
#include <QTransform>

CameraOrientationNormalizer::CameraOrientationNormalizer( QObject *parent )
  : QObject( parent )
{
  QScreen *screen = QGuiApplication::primaryScreen();
  if ( screen )
  {
    mCurrentOrientation = screen->orientation();
    connect( screen, &QScreen::orientationChanged, this, &CameraOrientationNormalizer::handleScreenOrientationChanged );
  }
}

int CameraOrientationNormalizer::previewRotation() const
{
  return mPreviewRotation;
}

void CameraOrientationNormalizer::recordCaptureOrientation()
{
  QScreen *screen = QGuiApplication::primaryScreen();
  mCaptureOrientation = screen ? screen->orientation() : Qt::PortraitOrientation;
  mCaptureOrientationRecorded = true;
}

void CameraOrientationNormalizer::recordCaptureViewportOrientation( bool landscape )
{
  QScreen *screen = QGuiApplication::primaryScreen();
  const Qt::ScreenOrientation screenOrientation = screen ? screen->orientation() : Qt::PrimaryOrientation;
  const bool screenIsLandscape = screenOrientation == Qt::LandscapeOrientation
                                 || screenOrientation == Qt::InvertedLandscapeOrientation;
  const bool screenIsPortrait = screenOrientation == Qt::PortraitOrientation
                                || screenOrientation == Qt::InvertedPortraitOrientation;

  // Preserve the clockwise/counter-clockwise distinction whenever Qt reports
  // an orientation consistent with the visible viewport. If Android keeps the
  // screen enum locked, the viewport still gives us the correct aspect.
  if ( ( landscape && screenIsLandscape ) || ( !landscape && screenIsPortrait ) )
    mCaptureOrientation = screenOrientation;
  else
    mCaptureOrientation = landscape ? Qt::LandscapeOrientation : Qt::PortraitOrientation;
  mCaptureOrientationRecorded = true;
}

bool CameraOrientationNormalizer::normalizeImageOrientation( const QString &path )
{
#if defined( Q_OS_ANDROID ) || defined( Q_OS_IOS ) || defined( Q_OS_WIN )
  if ( path.isEmpty() )
  {
    return false;
  }

  // Only a normalizer which observed the shutter event may infer orientation
  // from width/height. This keeps a second validation pass from rotating an
  // already-normalized landscape photo toward the default portrait state.
  const bool captureOrientationRecorded = mCaptureOrientationRecorded;
  mCaptureOrientationRecorded = false;

  // Read the EXIF transform before decoding. When present, let Qt apply it to
  // the pixels so the rewritten file is oriented correctly even in viewers
  // which ignore EXIF. The capture-orientation heuristic is only a fallback
  // for camera backends that write rotated pixels without an EXIF tag.
  QImageReader metadataReader( path );
  metadataReader.setAutoTransform( false );
  const QImageIOHandler::Transformations exifTransform = metadataReader.transformation();
  const QByteArray imageFormat = metadataReader.format();

  QImageReader reader( path );
  const bool hasExifTransform = exifTransform != QImageIOHandler::TransformationNone;
  reader.setAutoTransform( hasExifTransform );
  QImage image = reader.read();
  if ( image.isNull() )
  {
    return false;
  }

  const bool capturedInLandscape = ( mCaptureOrientation == Qt::LandscapeOrientation || mCaptureOrientation == Qt::InvertedLandscapeOrientation );
  const bool pixelsAreLandscape = image.width() > image.height();
  const bool needsFallbackRotation = !hasExifTransform && captureOrientationRecorded && ( capturedInLandscape != pixelsAreLandscape );

  if ( !needsFallbackRotation && !hasExifTransform )
  {
    return false;
  }

  if ( needsFallbackRotation )
  {
    QTransform transform;
    int rotationDegrees = pixelsAreLandscape ? 90 : 270;
    if ( pixelsAreLandscape && mCaptureOrientation == Qt::InvertedPortraitOrientation )
      rotationDegrees = 270;
    else if ( !pixelsAreLandscape && mCaptureOrientation == Qt::InvertedLandscapeOrientation )
      rotationDegrees = 90;
    transform.rotate( rotationDegrees );
    image = image.transformed( transform, Qt::SmoothTransformation );
  }

  // QSaveFile commits with a same-directory rename, so a failed encode never
  // truncates the valid camera output that is already on disk.
  QSaveFile saveFile( path );
  if ( !saveFile.open( QIODevice::WriteOnly ) )
  {
    return false;
  }

  QImageWriter writer( &saveFile, imageFormat.isEmpty() ? QByteArrayLiteral( "jpg" ) : imageFormat );
  writer.setTransformation( QImageIOHandler::TransformationNone );
  writer.setQuality( 95 );
  if ( !writer.write( image ) )
  {
    saveFile.cancelWriting();
    return false;
  }
  return saveFile.commit();
#else
  Q_UNUSED( path )
  return false;
#endif
}

void CameraOrientationNormalizer::handleScreenOrientationChanged( Qt::ScreenOrientation orientation )
{
  if ( mCurrentOrientation == orientation )
  {
    return;
  }

  mCurrentOrientation = orientation;
  updatePreviewRotation();
}

void CameraOrientationNormalizer::updatePreviewRotation()
{
#if defined( Q_OS_IOS ) || defined( Q_OS_WIN )
  const QScreen *screen = QGuiApplication::primaryScreen();
  if ( !screen )
  {
    return;
  }

  const int screenAngle = screen->angleBetween( screen->nativeOrientation(), mCurrentOrientation );
  const bool isLandscape = ( screenAngle == 90 || screenAngle == 270 );
  const int rotation = isLandscape ? 180 : 0;

  if ( rotation != mPreviewRotation )
  {
    mPreviewRotation = rotation;
    emit previewRotationChanged();
  }
#endif
}
