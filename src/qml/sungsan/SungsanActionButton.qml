/*
 * Sungsan Mobile GIS field action button
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

Button {
  id: control

  property url iconSource
  property color accentColor: "#194793"
  property color foregroundColor: enabled ? "#17324d" : "#8b98a5"
  property string detailText: ""
  property bool emphasized: false
  property bool compact: false

  implicitWidth: compact ? 68 : 132
  implicitHeight: compact ? 68 : (detailText.length > 0 ? 82 : 68)
  leftPadding: compact ? 5 : 12
  rightPadding: compact ? 5 : 12
  topPadding: compact ? 5 : 9
  bottomPadding: compact ? 5 : 9

  contentItem: GridLayout {
    columns: control.compact ? 1 : 2
    columnSpacing: 10
    rowSpacing: 1

    Rectangle {
      Layout.preferredWidth: 40
      Layout.preferredHeight: 40
      Layout.alignment: Qt.AlignVCenter | (control.compact ? Qt.AlignHCenter : 0)
      radius: 12
      color: control.emphasized ? "#ffffff" : (control.down ? "#dbe7f7" : "#eaf0f8")

      Image {
        id: iconImage
        anchors.centerIn: parent
        width: 24
        height: 24
        source: control.iconSource
        sourceSize.width: 24
        sourceSize.height: 24
        visible: false
      }

      MultiEffect {
        anchors.fill: iconImage
        source: iconImage
        colorization: 1
        colorizationColor: control.accentColor
        opacity: control.enabled ? 1 : 0.42
      }
    }

    ColumnLayout {
      Layout.fillWidth: true
      Layout.alignment: Qt.AlignVCenter | (control.compact ? Qt.AlignHCenter : 0)
      spacing: 2

      Label {
        Layout.fillWidth: true
        text: control.text
        color: control.emphasized ? "#ffffff" : control.foregroundColor
        font.pixelSize: control.compact ? 12 : 16
        font.bold: true
        elide: Text.ElideRight
        horizontalAlignment: control.compact ? Text.AlignHCenter : Text.AlignLeft
      }

      Label {
        Layout.fillWidth: true
        visible: !control.compact && control.detailText.length > 0
        text: control.detailText
        color: control.emphasized ? "#dfeaff" : "#66788a"
        font.pixelSize: 11
        elide: Text.ElideRight
      }
    }
  }

  background: Rectangle {
    radius: 16
    color: {
      if (!control.enabled)
        return "#eef1f4";
      if (control.emphasized)
        return control.down ? "#123975" : control.accentColor;
      return control.down ? "#e2eaf4" : "#ffffff";
    }
    border.width: control.emphasized ? 0 : 1
    border.color: control.hovered ? control.accentColor : "#d9e1e9"
  }
}
