/*
 * Sungsan Mobile GIS primary field-work controls
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.qfield
import Theme

Item {
  id: root
  objectName: "sungsanFieldPanel"

  property bool projectLoaded: false
  property string projectName: ""
  property string activeLayerName: "선택된 조사 레이어 없음"
  property bool editMode: false
  property bool autoSaveEnabled: true
  property string lastSavedText: ""
  property bool gpsActive: false
  property bool gpsPositionValid: false
  property bool gpsSignalStale: false
  property real gpsAccuracy: -1
  property string gpsDeviceName: ""
  property string gpsQualityText: ""
  property int gpsSatellites: 0
  property bool vworldReady: false
  property bool canAddFeature: false
  property bool canEditExistingPoint: false
  property bool pointLayer: false
  property bool multiVertexLayer: false
  property bool existingPointSelectionPending: false
  property bool geometryInProgress: false
  property bool geometryValid: false
  property bool moreExpanded: false
  readonly property real dockHorizontalMargin: 12
  readonly property real actionSpacing: 8
  readonly property real actionButtonHeight: 80
  readonly property int actionColumns: width >= 600 ? 4 : (width >= 420 ? 3 : 2)
  readonly property real reservedBottom: workDock.height
  readonly property real reservedTop: fieldHeader.height

  signal homeRequested
  signal startSurveyRequested
  signal currentLocationRequested
  signal gnssSettingsRequested
  signal layersRequested
  signal addFeatureRequested
  signal editExistingPointRequested
  signal addVertexRequested
  signal removeVertexRequested
  signal confirmGeometryRequested
  signal cancelGeometryRequested
  signal manualSaveRequested
  signal autoSaveToggled(bool enabled)
  signal exportRequested

  visible: projectLoaded
  enabled: visible

  Rectangle {
    id: fieldHeader
    anchors.left: parent.left
    anchors.right: parent.right
    anchors.top: parent.top
    height: 72 + mainWindow.sceneTopMargin
    color: "#194793"

    RowLayout {
      anchors.left: parent.left
      anchors.right: parent.right
      anchors.bottom: parent.bottom
      anchors.leftMargin: 12 + mainWindow.sceneLeftMargin
      anchors.rightMargin: 12 + mainWindow.sceneRightMargin
      anchors.bottomMargin: 9
      height: 52
      spacing: 10

      Button {
        Layout.preferredWidth: 62
        Layout.preferredHeight: 44
        text: "‹ 뒤로"
        flat: true
        font.pixelSize: 14
        font.bold: true
        Accessible.name: "뒤로가기"
        onClicked: root.homeRequested()
        contentItem: Label {
          text: parent.text
          color: "white"
          horizontalAlignment: Text.AlignHCenter
          verticalAlignment: Text.AlignVCenter
          font: parent.font
        }
        background: Rectangle {
          radius: 13
          color: parent.down ? "#153d7d" : "#2a58a0"
        }
      }

      Button {
        Layout.preferredWidth: 64
        Layout.preferredHeight: 44
        text: "레이어"
        flat: true
        font.pixelSize: 13
        font.bold: true
        Accessible.name: "레이어 목록 열기"
        onClicked: root.layersRequested()
        contentItem: Label {
          text: parent.text
          color: "white"
          horizontalAlignment: Text.AlignHCenter
          verticalAlignment: Text.AlignVCenter
          font: parent.font
        }
        background: Rectangle {
          radius: 13
          color: parent.down ? "#153d7d" : "#2a58a0"
        }
      }

      ColumnLayout {
        Layout.fillWidth: true
        spacing: 0
        Label {
          Layout.fillWidth: true
          text: root.projectName.length > 0 ? root.projectName : "성산 현장지도"
          color: "#ffffff"
          font.pixelSize: 16
          font.bold: true
          elide: Text.ElideMiddle
        }
        Label {
          Layout.fillWidth: true
          text: root.activeLayerName
          color: "#dce8fb"
          font.pixelSize: 11
          elide: Text.ElideRight
        }
      }

      Rectangle {
        Layout.preferredWidth: 78
        Layout.preferredHeight: 38
        radius: 12
        color: root.vworldReady ? "#e7f6ee" : "#fff1d9"

        Column {
          anchors.centerIn: parent
          spacing: 0
          Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "VWorld"
            color: root.vworldReady ? "#18784a" : "#9a6500"
            font.pixelSize: 11
            font.bold: true
          }
          Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.vworldReady ? "영상 준비" : "선택 가능"
            color: root.vworldReady ? "#18784a" : "#9a6500"
            font.pixelSize: 9
          }
        }
      }
    }
  }

  Rectangle {
    id: workDock
    anchors.left: parent.left
    anchors.right: parent.right
    anchors.bottom: parent.bottom
    readonly property real contentHeight: dockColumn.implicitHeight + 20 + mainWindow.sceneBottomMargin
    height: Math.min(contentHeight, Math.max(0, root.height - fieldHeader.height - 4))
    color: "#f7f9fc"
    radius: 22
    border.color: "#d3dce6"

    Flickable {
      id: dockFlickable
      anchors.fill: parent
      clip: true
      contentWidth: width
      contentHeight: workDock.contentHeight
      interactive: contentHeight > height
      boundsBehavior: Flickable.StopAtBounds
      ScrollBar.vertical: ScrollBar {
        policy: dockFlickable.interactive ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
      }

      ColumnLayout {
        id: dockColumn
        x: root.dockHorizontalMargin + mainWindow.sceneLeftMargin
        y: 10
        width: Math.max(0, dockFlickable.width - root.dockHorizontalMargin * 2 - mainWindow.sceneLeftMargin - mainWindow.sceneRightMargin)
        spacing: root.actionSpacing

      RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Rectangle {
          Layout.preferredWidth: 9
          Layout.preferredHeight: 9
          radius: 5
          color: root.gpsPositionValid ? "#20a569" : root.gpsSignalStale ? "#c74444" : root.gpsActive ? "#e5a11a" : "#a4afb9"
        }
        Label {
          Layout.fillWidth: true
          text: {
            const configuredName = root.gpsDeviceName.trim();
            const deviceName = configuredName.length === 0 || configuredName === "Internal device"
              ? "휴대폰 GNSS"
              : configuredName;
            if (root.gpsPositionValid) {
              const parts = [deviceName];
              if (root.gpsQualityText.length > 0)
                parts.push(root.gpsQualityText);
              if (root.gpsSatellites > 0)
                parts.push("위성 " + root.gpsSatellites);
              if (root.gpsAccuracy >= 0)
                parts.push("±" + root.gpsAccuracy.toFixed(2) + " m");
              return parts.join(" · ");
            }
            if (root.gpsSignalStale)
              return deviceName + " 수신 끊김 · 이전 위치 사용 금지";
            return root.gpsActive ? deviceName + " 위치 수신 중" : "GNSS 꺼짐 · 외부 수신기 연결 가능";
          }
          color: "#516476"
          font.pixelSize: 11
          elide: Text.ElideRight
        }
        Label {
          text: root.autoSaveEnabled ? (root.lastSavedText.length > 0 ? "● 자동저장 · " + root.lastSavedText : "● 자동저장 켜짐") : "○ 자동저장 꺼짐"
          color: root.autoSaveEnabled ? "#18784a" : "#8b5f17"
          font.pixelSize: 11
          font.bold: true
        }
      }

      GridLayout {
        Layout.fillWidth: true
        columns: root.actionColumns
        columnSpacing: root.actionSpacing
        rowSpacing: root.actionSpacing

        SungsanActionButton {
          Layout.fillWidth: true
          Layout.preferredHeight: root.actionButtonHeight
          text: "현재 위치"
          compact: true
          iconSource: Theme.getThemeVectorIcon("ic_location_white_24dp")
          onClicked: root.currentLocationRequested()
        }

        SungsanActionButton {
          Layout.fillWidth: true
          Layout.preferredHeight: root.actionButtonHeight
          text: root.pointLayer ? "지점 추가" : "객체 추가"
          compact: true
          detailText: root.pointLayer ? "화면 중심 또는 GPS 위치" : "선·면 그리기 시작"
          iconSource: Theme.getThemeVectorIcon("ic_add_white_24dp")
          enabled: root.canAddFeature && !root.geometryInProgress
          emphasized: root.editMode && !root.geometryInProgress
          onClicked: root.addFeatureRequested()
        }

        SungsanActionButton {
          Layout.fillWidth: true
          Layout.preferredHeight: root.actionButtonHeight
          text: root.editMode ? "조사 중" : "조사 시작"
          compact: true
          iconSource: Theme.getThemeVectorIcon("ic_create_white_24dp")
          emphasized: root.editMode
          onClicked: root.startSurveyRequested()
        }

        SungsanActionButton {
          Layout.fillWidth: true
          Layout.preferredHeight: root.actionButtonHeight
          text: "수동 저장"
          compact: true
          iconSource: Theme.getThemeVectorIcon("ic_check_white_24dp")
          onClicked: root.manualSaveRequested()
        }
      }

      SungsanActionButton {
        visible: root.pointLayer
        Layout.fillWidth: true
        Layout.preferredHeight: root.actionButtonHeight
        text: root.existingPointSelectionPending ? "지점 선택 취소" : "지점 사진·속성"
        compact: true
        detailText: root.existingPointSelectionPending ? "지도 선택 대기 중" : "기존 지점의 근경·원경·기타·추가사진"
        iconSource: Theme.getThemeVectorIcon(root.existingPointSelectionPending ? "ic_clear_white_24dp" : "ic_create_white_24dp")
        enabled: root.canEditExistingPoint && !root.geometryInProgress
        emphasized: root.existingPointSelectionPending
        onClicked: root.editExistingPointRequested()
      }

      Button {
        Layout.fillWidth: true
        Layout.preferredHeight: 34
        flat: true
        text: root.moreExpanded ? "간단히 보기 ︿" : "더보기 · 자동저장 / 결과 내보내기 ﹀"
        onClicked: root.moreExpanded = !root.moreExpanded
        contentItem: Label {
          text: parent.text
          color: "#194793"
          font.pixelSize: 12
          font.bold: true
          horizontalAlignment: Text.AlignHCenter
          verticalAlignment: Text.AlignVCenter
        }
      }

      GridLayout {
        visible: root.moreExpanded
        Layout.fillWidth: true
        columns: Math.min(2, root.actionColumns)
        columnSpacing: root.actionSpacing
        rowSpacing: root.actionSpacing

        SungsanActionButton {
          Layout.fillWidth: true
          Layout.columnSpan: 2
          Layout.preferredHeight: root.actionButtonHeight
          text: "외부 GNSS 연결"
          compact: true
          detailText: "CHCNAV 포함 표준 NMEA · Bluetooth/BLE · TCP/UDP"
          iconSource: Theme.getThemeVectorIcon("ic_bluetooth_receiver_black_24dp")
          onClicked: root.gnssSettingsRequested()
        }

        SungsanActionButton {
          Layout.fillWidth: true
          Layout.preferredHeight: root.actionButtonHeight
          text: root.autoSaveEnabled ? "자동저장 켬" : "자동저장 끔"
          compact: true
          iconSource: Theme.getThemeVectorIcon(root.autoSaveEnabled ? "ic_update_white_24dp" : "ic_pause_black_24dp")
          onClicked: root.autoSaveToggled(!root.autoSaveEnabled)
        }

        SungsanActionButton {
          Layout.fillWidth: true
          Layout.preferredHeight: root.actionButtonHeight
          text: "결과 내보내기"
          compact: true
          detailText: "현장 폴더를 ZIP으로 공유"
          iconSource: Theme.getThemeVectorIcon("ic_cloud_upload_24dp")
          onClicked: root.exportRequested()
        }

      }

      GridLayout {
        visible: root.editMode && root.multiVertexLayer
        Layout.fillWidth: true
        columns: root.actionColumns
        columnSpacing: root.actionSpacing
        rowSpacing: root.actionSpacing

        SungsanActionButton {
          Layout.fillWidth: true
          Layout.preferredHeight: root.actionButtonHeight
          text: "꼭짓점 추가"
          compact: true
          detailText: "선·면을 이어서 그리기"
          iconSource: Theme.getThemeVectorIcon("ic_add_vertex_white_24dp")
          emphasized: true
          onClicked: root.addVertexRequested()
        }

        SungsanActionButton {
          Layout.fillWidth: true
          Layout.preferredHeight: root.actionButtonHeight
          text: "한 점 취소"
          compact: true
          iconSource: Theme.getThemeVectorIcon("ic_remove_vertex_white_24dp")
          enabled: root.geometryInProgress
          onClicked: root.removeVertexRequested()
        }

        SungsanActionButton {
          Layout.fillWidth: true
          Layout.preferredHeight: root.actionButtonHeight
          text: "도형 완료"
          compact: true
          iconSource: Theme.getThemeVectorIcon("ic_check_white_24dp")
          enabled: root.geometryValid
          emphasized: root.geometryValid
          onClicked: root.confirmGeometryRequested()
        }

        SungsanActionButton {
          Layout.fillWidth: true
          Layout.preferredHeight: root.actionButtonHeight
          text: "도형 취소"
          compact: true
          iconSource: Theme.getThemeVectorIcon("ic_clear_white_24dp")
          enabled: root.geometryInProgress
          accentColor: "#a53535"
          onClicked: root.cancelGeometryRequested()
        }
      }
      }
    }
  }
}
