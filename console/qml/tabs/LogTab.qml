import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RobotGui

// Log 탭 — 로봇이 [F0 EE] 프레임으로 보내온 로그.
//
// 색이 의미를 갖는 몇 안 되는 화면이다 (Theme 주석의 "색은 상태만 의미한다").
// 그래서 주변 크롬은 최대한 조용히 두고 글자색만 말하게 한다.
Item {
    id: root

    required property var logs   // Bridge.logs

    // TLogLevel: 0=ERROR 1=WARNING 2=SUCCESS 3=INFO 4=DEBUG
    function levelColor(lv) {
        switch (lv) {
        case 0:  return Theme.logError
        case 1:  return Theme.logWarning
        case 2:  return Theme.logSuccess
        case 3:  return Theme.logInfo
        default: return Theme.logDebug
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.pad2
        spacing: Theme.gap

        // ── 도구 줄 ───────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap

            Button {
                Layout.preferredWidth: 110
                Layout.preferredHeight: 36
                // 라벨이 상태를 말한다 — 멈춰 있으면 다음 동작은 재개다.
                text: root.logs.paused ? qsTr("Resume") : qsTr("Pause")
                onClicked: root.logs.paused = !root.logs.paused
                background: Rectangle {
                    color: parent.down ? Theme.ground : Theme.surface
                    border.color: root.logs.paused ? Theme.warn : Theme.rule
                    border.width: root.logs.paused ? 2 : 1

                }
                contentItem: Label {
                    text: parent.text
                    color: root.logs.paused ? Theme.warn : Theme.graphite
                    font.family: Theme.sans
                    font.pixelSize: Theme.fsBody
                    font.weight: Font.Medium
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            ComboBox {
                Layout.preferredWidth: 150
                Layout.preferredHeight: 36
                font.family: Theme.sans
                font.pixelSize: Theme.fsBody
                // 콤보 순서 → 필터 레벨. TLogLevel 이 심각도 오름차순이라
                // "이 값 이하만 표시" 로 처리된다 (원본 onLogLevelChanged 와 동일).
                model: [qsTr("ALL"), qsTr("ERROR"), qsTr("WARNING"), qsTr("INFO")]
                onCurrentIndexChanged: {
                    root.logs.filterLevel = [4, 0, 1, 3][currentIndex]
                }
            }

            Button {
                Layout.preferredWidth: 110
                Layout.preferredHeight: 36
                text: qsTr("Clear")
                onClicked: root.logs.clear()
                background: Rectangle {
                    color: parent.down ? Theme.ground : Theme.surface
                    border.color: Theme.rule

                }
                contentItem: Label {
                    text: parent.text
                    color: Theme.graphite
                    font.family: Theme.sans
                    font.pixelSize: Theme.fsBody
                    font.weight: Font.Medium
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Item { Layout.fillWidth: true }

            Label {
                text: root.logs.countText
                color: Theme.mist
                font.family: Theme.mono
                font.pixelSize: Theme.fsSmall
            }
        }

        // ── 로그 뷰 ───────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surface
            border.color: Theme.rule

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 8
                model: root.logs
                clip: true
                spacing: 1
                // 줄바꿈 없이 가로 스크롤 — 원본도 NoWrap 이다. 로그는 열이 맞아야
                // 훑을 수 있고, 접히면 그게 무너진다.
                contentWidth: contentItem.childrenRect.width
                flickableDirection: Flickable.HorizontalAndVerticalFlick
                boundsBehavior: Flickable.StopAtBounds

                delegate: Label {
                    required property int level
                    required property string message
                    text: message
                    color: root.levelColor(level)
                    font.family: Theme.mono
                    font.pixelSize: Theme.fsSmall
                    // PlainText 로 두면 연속 공백이 그대로 남는다 — 원본이 &nbsp;
                    // 치환 루프로 우회하던 문제가 여기서는 발생하지 않는다.
                    textFormat: Text.PlainText
                }

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

                // 맨 아래에 있을 때만 따라 내려간다. 위로 올려 읽는 중에
                // 끌려가는 건 로그 뷰어에서 제일 흔한 실수다 (원본 주석).
                property bool atBottom: contentY >= contentHeight - height - 8
                onCountChanged: if (atBottom) positionViewAtEnd()
            }

            Label {
                anchors.centerIn: parent
                visible: root.logs.totalCount === 0
                text: qsTr("로봇이 로그를 보내면 여기에 표시됩니다")
                color: Theme.mist
                font.family: Theme.sans
                font.pixelSize: Theme.fsBody
            }
        }
    }
}
