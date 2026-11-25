/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QImageReader>
#include <QImageWriter>
#include <QSaveFile>
#include <QTransform>

// Decode once. The returned file can be viewed with or without EXIF support.
inline bool prepareCapturedImage( const QString &path, Qt::ScreenOrientation orientation, bool recorded )
{
  // Read the EXIF transform before decoding. When present, let Qt apply it to
  // the pixels so the rewritten file is oriented correctly even in viewers
  // which ignore EXIF. The capture-orientation heuristic is only a fallback
  // for camera backends that write rotated pixels without an EXIF tag.
  QImage image;
  QByteArray imageFormat;
  bool hasExifTransform;
  {
    QImageReader reader( path );
    hasExifTransform = reader.transformation() != QImageIOHandler::TransformationNone;
    reader.setAutoTransform( hasExifTransform );
    imageFormat = reader.format();
    image = reader.read();
  }
  if ( image.isNull() )
  {
    return false;
  }

  const bool capturedInLandscape = ( orientation == Qt::LandscapeOrientation || orientation == Qt::InvertedLandscapeOrientation );
  const bool pixelsAreLandscape = image.width() > image.height();
  const bool needsFallbackRotation = !hasExifTransform && recorded && ( capturedInLandscape != pixelsAreLandscape );

  if ( !needsFallbackRotation && !hasExifTransform )
  {
    return true;
  }

  if ( needsFallbackRotation )
  {
    QTransform transform;
    int rotationDegrees = pixelsAreLandscape ? 90 : 270;
    if ( pixelsAreLandscape && orientation == Qt::InvertedPortraitOrientation )
      rotationDegrees = 270;
    else if ( !pixelsAreLandscape && orientation == Qt::InvertedLandscapeOrientation )
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
}
