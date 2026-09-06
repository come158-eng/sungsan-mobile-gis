/*
 * Meta Engineering GIS field action button
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Layouts

Button {
  id: control

  property url iconSource
  property color accentColor: "#194793"
  property color foregroundColor: enabled ? "#17324d" : "#8b98a5"
  property string detailText: ""
  property bool emphasized: false
  property bool compact: false
  readonly property real contentSpacing: 8
  readonly property real verticalContentPadding: 12
  readonly property real compactIconBoxSize: 32
  readonly property real iconGlyphSize: 22

  implicitWidth: compact ? 72 : 132
  implicitHeight: compact ? 80 : (detailText.length > 0 ? 82 : 68)
  leftPadding: 12
  rightPadding: 12
  topPadding: verticalContentPadding
  bottomPadding: verticalContentPadding

  contentItem: Item {
    implicitWidth: contentLayout.implicitWidth
    implicitHeight: contentLayout.implicitHeight

    GridLayout {
      id: contentLayout
      anchors.centerIn: parent
      width: parent.width
      columns: control.compact ? 1 : 2
      columnSpacing: control.contentSpacing
      rowSpacing: control.compact ? control.contentSpacing : 2

      Rectangle {
        Layout.preferredWidth: control.compact ? control.compactIconBoxSize : 40
        Layout.preferredHeight: control.compact ? control.compactIconBoxSize : 40
        Layout.alignment: Qt.AlignVCenter | (control.compact ? Qt.AlignHCenter : 0)
        radius: 12
        color: control.emphasized ? "#ffffff" : (control.down ? "#dbe7f7" : "#eaf0f8")

        IconLabel {
          anchors.centerIn: parent
          icon.source: control.iconSource
          icon.width: control.iconGlyphSize
          icon.height: control.iconGlyphSize
          icon.color: control.accentColor
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
          verticalAlignment: Text.AlignVCenter
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
