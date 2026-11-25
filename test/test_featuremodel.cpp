/***************************************************************************
                        test_featuremodel.h
                        --------------------
  begin                : Oct 2022
  copyright            : (C) 2022 by Mathieu Pellerin
  email                : mathieu@opengis.ch
***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "appexpressioncontextscopesgenerator.h"
#include "catch2.h"
#include "featuremodel.h"
#include "gnsspositioninformation.h"

#include <QAbstractItemModelTester>
#include <qgsvectorlayer.h>

TEST_CASE( "FeatureModel" )
{
  std::unique_ptr<QgsVectorLayer> layer = std::make_unique<QgsVectorLayer>( QStringLiteral( "Point?crs=EPSG:4326&field=name:text&field=horizontal_accuracy:double(12,6)&field=vertical_accuracy:double(12,6)" ), QStringLiteral( "vl" ), QStringLiteral( "memory" ) );
  layer->setDefaultValueDefinition( 1, QgsDefaultValue( QStringLiteral( "@position_horizontal_accuracy" ), true ) );
  layer->setDefaultValueDefinition( 2, QgsDefaultValue( QStringLiteral( "@position_vertical_accuracy" ), true ) );
  std::unique_ptr<FeatureModel> featureModel = std::make_unique<FeatureModel>();

  std::unique_ptr<AppExpressionContextScopesGenerator> appExpressionContextScopesGenerator = std::make_unique<AppExpressionContextScopesGenerator>();
  featureModel->setAppExpressionContextScopesGenerator( appExpressionContextScopesGenerator.get() );

  GnssPositionInformation position( 1.1, 2.2, 50.0, 50.0, 0.0, QList<QgsSatelliteInfo>(), 0, 0, 0, 5.5, 10.5, QDateTime(), QChar(), 0, 100 );

  featureModel->setCurrentLayer( layer.get() );
  featureModel->appExpressionContextScopesGenerator()->setPositionInformation( position );
  featureModel->appExpressionContextScopesGenerator()->setPositionLocked( true );
  featureModel->resetFeature();
  featureModel->resetAttributes();
  featureModel->setData( featureModel->index( 0, 0 ), QStringLiteral( "created" ), FeatureModel::AttributeValue );
  featureModel->create();

  QgsFeature feature = featureModel->feature();
  REQUIRE( feature.attribute( 0 ).toString() == QStringLiteral( "created" ) );
  REQUIRE( feature.attribute( 1 ).toDouble() == 5.5 );
  REQUIRE( feature.attribute( 2 ).toDouble() == 10.5 );

  featureModel->setData( featureModel->index( 0, 0 ), QStringLiteral( "updated" ), FeatureModel::AttributeValue );
  featureModel->save();

  feature = featureModel->feature();
  REQUIRE( feature.attribute( 0 ).toString() == QStringLiteral( "updated" ) );
  REQUIRE( feature.attribute( 1 ).toDouble() == 5.5 );
  REQUIRE( feature.attribute( 2 ).toDouble() == 10.5 );

  std::unique_ptr<FeatureModel> modelTest = std::make_unique<FeatureModel>();
  std::unique_ptr<QAbstractItemModelTester> modelTester = std::make_unique<QAbstractItemModelTester>( modelTest.get(), QAbstractItemModelTester::FailureReportingMode::Fatal );
}

TEST_CASE( "Field form keeps the capture time and rejects partial photo updates" )
{
  QgsVectorLayer layer( QStringLiteral( "Point?crs=EPSG:4326&field=name:string&field=surveyed_at:datetime&field=photo:string" ), QStringLiteral( "survey" ), QStringLiteral( "memory" ) );
  layer.setCustomProperty( QStringLiteral( "kr.co.sungsan.mobilegis/fieldObjects" ), true );
  const int fixedField = layer.addExpressionField( QStringLiteral( "'fixed'" ), QgsField( QStringLiteral( "readonly_note" ), QMetaType::QString ) );
  REQUIRE( fixedField >= 0 );
  FeatureModel model;
  model.setCurrentLayer( &layer );
  model.resetFeature();
  model.resetAttributes();
  const QDateTime before = QDateTime::currentDateTime();
  model.stampFieldSurveyTime();
  const QDateTime captured = model.feature().attribute( 1 ).toDateTime();
  REQUIRE( captured >= before );
  REQUIRE( model.setData( model.index( 2 ), QStringLiteral( "original.jpg" ), FeatureModel::AttributeValue ) );
  REQUIRE( model.create() );
  const QgsFeatureId id = model.feature().id();
  REQUIRE( layer.getFeature( id ).attribute( 1 ).toDateTime() == captured );
  model.stampFieldSurveyTime();

  REQUIRE( layer.startEditing() );
  REQUIRE( layer.changeAttributeValue( id, 0, QStringLiteral( "unrelated" ) ) );
  REQUIRE( model.setData( model.index( 2 ), QStringLiteral( "new.jpg" ), FeatureModel::AttributeValue ) );
  REQUIRE( model.setData( model.index( fixedField ), QStringLiteral( "invalid" ), FeatureModel::AttributeValue ) );
  REQUIRE_FALSE( model.save() );
  REQUIRE( layer.isEditable() );
  REQUIRE( layer.getFeature( id ).attribute( 0 ).toString() == QStringLiteral( "unrelated" ) );
  REQUIRE( layer.getFeature( id ).attribute( 2 ).toString() == QStringLiteral( "original.jpg" ) );
  REQUIRE( model.feature().attribute( 2 ).toString() == QStringLiteral( "new.jpg" ) );

  REQUIRE( model.setData( model.index( fixedField ), QStringLiteral( "fixed" ), FeatureModel::AttributeValue ) );
  REQUIRE( model.save() );
  REQUIRE( layer.getFeature( id ).attribute( 1 ).toDateTime() == captured );
  REQUIRE( layer.getFeature( id ).attribute( 2 ).toString() == QStringLiteral( "new.jpg" ) );
}

TEST_CASE( "Batch and multi-feature form saves return provider results" )
{
  QgsVectorLayer layer( QStringLiteral( "Point?field=photo:string" ), QStringLiteral( "survey" ), QStringLiteral( "memory" ) );
  const int virtualIndex = layer.addExpressionField( QStringLiteral( "'fixed'" ), QgsField( QStringLiteral( "calculated_note" ), QMetaType::QString ) );
  REQUIRE( layer.startEditing() );
  QgsFeature first( layer.fields() );
  QgsFeature second( layer.fields() );
  first.setAttribute( 0, QStringLiteral( "original" ) );
  second.setAttribute( 0, QStringLiteral( "original" ) );
  REQUIRE( layer.addFeature( first ) );
  REQUIRE( layer.addFeature( second ) );
  REQUIRE( layer.commitChanges() );
  QList<QgsFeature> rows;
  QgsFeature f;
  QgsFeatureIterator it = layer.getFeatures();
  while ( it.nextFeature( f ) )
    rows.append( f );
  REQUIRE( rows.size() == 2 );
  FeatureModel model;
  model.setCurrentLayer( &layer );
  model.setModelMode( FeatureModel::MultiFeatureModel );
  model.setFeatures( rows );
  REQUIRE( model.setData( model.index( 0 ), QStringLiteral( "replacement" ), FeatureModel::AttributeValue ) );
  REQUIRE( model.setData( model.index( virtualIndex ), QStringLiteral( "invalid" ), FeatureModel::AttributeValue ) );
  REQUIRE_FALSE( model.save() );
  for ( const QgsFeature &row : rows )
    REQUIRE( layer.getFeature( row.id() ).attribute( 0 ).toString() == QStringLiteral( "original" ) );
  model.setModelMode( FeatureModel::SingleFeatureModel );
  model.setFeature( rows.first() );
  model.setBatchMode( true );
  REQUIRE( model.setData( model.index( 0 ), QStringLiteral( "batch" ), FeatureModel::AttributeValue ) );
  REQUIRE( model.save() );
  model.setBatchMode( false );
  REQUIRE( layer.getFeature( rows.first().id() ).attribute( 0 ).toString() == QStringLiteral( "batch" ) );
}
