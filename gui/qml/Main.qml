import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Effects
import SecureVault.Gui

ApplicationWindow {
    id: win
    width: 1120; height: 720; minimumWidth: 860; minimumHeight: 560
    visible: true; color: Theme.bg; title: "SecureVault"
    font.family: Theme.ui; font.pixelSize: 13
    property string toastText: ""
    Component.onCompleted: { var g = app.windowGeometry()
        if (g.w) { width = Math.max(g.w, minimumWidth); height = Math.max(g.h, minimumHeight)
            x = Math.min(Math.max(g.x, 0), Math.max(Screen.desktopAvailableWidth - 120, 0)); y = Math.min(Math.max(g.y, 0), Math.max(Screen.desktopAvailableHeight - 120, 0)) } }
    onClosing: app.saveWindowGeometry(x, y, width, height)
    function toast(t) { toastText = t; toastTimer.restart() }
    Timer { id: toastTimer; interval: 2500; onTriggered: win.toastText = "" }

    // ---------- shared pieces ----------
    component Field: TextField {
        color: Theme.text; placeholderTextColor: Theme.muted; selectByMouse: true
        leftPadding: 12; implicitHeight: 40
        background: Rectangle { radius: Theme.rSmall; color: Theme.surface2
            border.width: parent.activeFocus ? 2 : 1; border.color: parent.activeFocus ? Theme.accent : Theme.borderStrong }
    }
    component SecretField: Field {
        echoMode: TextInput.Password
        inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
    }
    component PrimaryButton: Button {
        id: b; implicitHeight: 40; leftPadding: 18; rightPadding: 18
        contentItem: Text { text: b.text; color: "white"; font.weight: Font.Medium; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { radius: Theme.rSmall; color: b.enabled ? Theme.accentFill : Theme.surface3
            border.width: b.visualFocus ? 2 : 0; border.color: Theme.accent; opacity: b.pressed ? 0.85 : 1
            Behavior on opacity { NumberAnimation { duration: app.reduceMotion ? 0 : 120 } } }
    }
    component GhostButton: Button {
        id: g; implicitHeight: 36; leftPadding: 12; rightPadding: 12
        contentItem: Text { text: g.text; color: Theme.text2; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { radius: Theme.rSmall; color: g.hovered ? Theme.surface3 : "transparent"
            border.width: g.visualFocus ? 2 : 0; border.color: Theme.accent }
    }

    // Add / edit. Editing never loads the stored password; a revealed value lives only in `shown`.
    component CredDialog: Popup {
        id: d
        onOpened: app.draftOpen(true)
        onClosed: app.draftOpen(false)
        property int editId: 0
        property bool changing: false
        property bool confirming: false
        property string shown: ""
        property string genInfo: ""
        property var strength: fPw.text !== "" ? app.assess(fPw.text) : null
        modal: true; focus: true; padding: 20; width: 480; anchors.centerIn: Overlay.overlay
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { radius: Theme.rLarge; color: Theme.surface3; border.color: Theme.border }
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: app.reduceMotion ? 0 : 200 } }
        function openFor(id) {
            editId = id; changing = id === 0; confirming = false; shown = ""; genInfo = ""; fPw.clear()
            var x = id ? app.detail(id) : {}
            fTitle.text = x.title || ""; fSite.text = x.site || ""; fUser.text = x.username || ""
            fCat.text = x.category || ""; fNotes.text = x.notes || ""
            open(); fTitle.forceActiveFocus()
        }
        onClosed: { shown = ""; fPw.clear() }
        Timer { id: hideTimer; interval: 15000; running: d.shown !== ""; onTriggered: d.shown = "" }
        Connections { target: Qt.application; function onStateChanged() { if (Qt.application.state !== Qt.ApplicationActive) d.shown = "" } }
        contentItem: ColumnLayout { spacing: 10
            Text { text: d.editId ? "Edit password" : "Add password"; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
            Field { id: fTitle; Layout.fillWidth: true; placeholderText: "Title" }
            Field { id: fSite; Layout.fillWidth: true; placeholderText: "Website (optional)" }
            Field { id: fUser; Layout.fillWidth: true; placeholderText: "Username" }
            Field { id: fCat; Layout.fillWidth: true; placeholderText: "Category (optional)" }
            // stored password: masked, explicit reveal, auto-hides after 15 s
            RowLayout { visible: d.editId !== 0 && !d.changing; Layout.fillWidth: true
                Text { Layout.fillWidth: true; color: Theme.text; font.family: Theme.mono; elide: Text.ElideRight
                    text: d.shown !== "" ? d.shown : "••••••••••••" }
                GhostButton { text: d.shown !== "" ? "Hide" : "Reveal"; onClicked: d.shown = d.shown !== "" ? "" : app.revealPassword(d.editId) }
                GhostButton { text: "Change password"; onClicked: { d.shown = ""; d.changing = true } } }
            RowLayout { visible: d.changing; Layout.fillWidth: true
                SecretField { id: fPw; Layout.fillWidth: true; placeholderText: d.editId ? "New password" : "Password" }
                GhostButton { text: "Generate"; onClicked: { var g = app.generate(20, true, true, true, true, true)
                    if (g.ok) { fPw.text = g.text; fPw.echoMode = TextInput.Normal; d.genInfo = "20 random characters, about " + g.bits + " bits" } } } }
            Text { visible: d.changing && d.strength !== null; color: d.strength ? Theme.strengthColor(d.strength.label) : Theme.text2
                text: d.strength ? d.strength.label + (d.strength.reason !== "" ? ": " + d.strength.reason : "") : "" }
            Text { visible: d.genInfo !== "" && d.changing; color: Theme.text2; text: d.genInfo }
            TextArea { id: fNotes; Layout.fillWidth: true; Layout.preferredHeight: 72; placeholderText: "Notes"; wrapMode: TextEdit.Wrap
                color: Theme.text; placeholderTextColor: Theme.muted; leftPadding: 12; topPadding: 8
                background: Rectangle { radius: Theme.rSmall; color: Theme.surface2; border.width: parent.activeFocus ? 2 : 1; border.color: parent.activeFocus ? Theme.accent : Theme.borderStrong } }
            Text { visible: app.error !== ""; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.dangerText; text: app.error }
            Text { visible: d.confirming; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.text
                text: "Delete \"" + fTitle.text + "\"? Its password and history are removed from this device and can't be recovered." }
            RowLayout { Layout.fillWidth: true
                GhostButton { visible: d.editId !== 0 && !d.confirming; text: "Delete"; palette.buttonText: Theme.dangerText; onClicked: d.confirming = true }
                Button { visible: d.confirming; text: "Delete"; implicitHeight: 40; onClicked: { if (app.removeCredential(d.editId)) { d.close(); win.toast("Deleted") } }
                    contentItem: Text { text: parent.text; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { radius: Theme.rSmall; color: Theme.dangerFill } }
                GhostButton { visible: d.confirming; text: "Keep"; onClicked: d.confirming = false }
                Item { Layout.fillWidth: true }
                GhostButton { text: "Cancel"; onClicked: d.close() }
                PrimaryButton { text: "Save"; enabled: !d.confirming
                    onClicked: if (app.saveCredential({id: d.editId, title: fTitle.text, site: fSite.text, username: fUser.text,
                        category: fCat.text, notes: fNotes.text, password: d.changing ? fPw.text : ""})) { d.close(); win.toast("Saved") } } }
        }
    }

    component NoteDialog: Popup {
        id: n
        onOpened: app.draftOpen(true)
        onClosed: app.draftOpen(false)
        property int editId: 0
        property bool confirming: false
        modal: true; focus: true; padding: 20; width: 520; anchors.centerIn: Overlay.overlay
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { radius: Theme.rLarge; color: Theme.surface3; border.color: Theme.border }
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: app.reduceMotion ? 0 : 200 } }
        function openFor(id) { editId = id; confirming = false
            var x = id ? app.noteDetail(id) : {}   // opening a note is the explicit action that loads its content
            nTitle.text = x.title || ""; nCat.text = x.category || ""; nBody.text = x.content || ""
            open(); nTitle.forceActiveFocus() }
        onClosed: nBody.text = ""
        Connections { target: Qt.application; function onStateChanged() { if (Qt.application.state !== Qt.ApplicationActive && !n.opened) nBody.text = "" } }
        contentItem: ColumnLayout { spacing: 10
            Text { text: n.editId ? "Edit note" : "Add note"; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
            Field { id: nTitle; Layout.fillWidth: true; placeholderText: "Title" }
            Field { id: nCat; Layout.fillWidth: true; placeholderText: "Category (optional)" }
            TextArea { id: nBody; Layout.fillWidth: true; Layout.preferredHeight: 200; placeholderText: "Write your note"; wrapMode: TextEdit.Wrap
                color: Theme.text; placeholderTextColor: Theme.muted; leftPadding: 12; topPadding: 8; font.family: Theme.mono
                inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                background: Rectangle { radius: Theme.rSmall; color: Theme.surface2; border.width: parent.activeFocus ? 2 : 1; border.color: parent.activeFocus ? Theme.accent : Theme.borderStrong } }
            Text { visible: app.error !== ""; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.dangerText; text: app.error }
            Text { visible: n.confirming; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.text
                text: "Delete \"" + nTitle.text + "\"? The note is removed from this device and can't be recovered." }
            RowLayout { Layout.fillWidth: true
                GhostButton { visible: n.editId !== 0 && !n.confirming; text: "Delete"; onClicked: n.confirming = true }
                Button { visible: n.confirming; text: "Delete"; implicitHeight: 40; onClicked: { if (app.removeNote(n.editId)) { n.close(); win.toast("Deleted") } }
                    contentItem: Text { text: parent.text; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { radius: Theme.rSmall; color: Theme.dangerFill } }
                GhostButton { visible: n.confirming; text: "Keep"; onClicked: n.confirming = false }
                Item { Layout.fillWidth: true }
                GhostButton { text: "Cancel"; onClicked: n.close() }
                PrimaryButton { text: "Save"; enabled: !n.confirming
                    onClicked: if (app.saveNote({id: n.editId, title: nTitle.text, category: nCat.text, content: nBody.text})) { n.close(); win.toast("Saved") } } }
        }
    }

    // One secret field (number or CVV): masked, explicit reveal (15 s), copy in C++, or type a replacement.
    component CardSecret: ColumnLayout {
        id: cs
        property int cardId: 0
        property string field: "number"
        property string label: ""
        property bool changing: true
        property string shown: ""
        property alias value: inp.text
        function reset() { shown = ""; inp.clear(); changing = cardId === 0 }
        spacing: 4
        RowLayout { visible: !cs.changing; Layout.fillWidth: true
            Text { text: cs.label; color: Theme.text2; Layout.preferredWidth: 90 }
            Text { Layout.fillWidth: true; font.family: Theme.mono; color: Theme.text; elide: Text.ElideRight; text: cs.shown !== "" ? cs.shown : "••••••••" }
            GhostButton { text: cs.shown !== "" ? "Hide" : "Reveal"; onClicked: cs.shown = cs.shown !== "" ? "" : app.revealCardField(cs.cardId, cs.field) }
            GhostButton { text: "Copy"; onClicked: win.toast(app.copyCardField(cs.cardId, cs.field) ? "Copied" : "Copying isn't available on this platform.") }
            GhostButton { text: "Change"; onClicked: { cs.shown = ""; cs.changing = true } } }
        SecretField { id: inp; visible: cs.changing; Layout.fillWidth: true; placeholderText: cs.label }
        Timer { interval: 15000; running: cs.shown !== ""; onTriggered: cs.shown = "" }
        Connections { target: Qt.application; function onStateChanged() { if (Qt.application.state !== Qt.ApplicationActive) cs.shown = "" } }
    }
    component CardDialog: Popup {
        id: k
        onOpened: app.draftOpen(true)
        onClosed: app.draftOpen(false)
        property int editId: 0
        property bool confirming: false
        modal: true; focus: true; padding: 20; width: 500; anchors.centerIn: Overlay.overlay
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { radius: Theme.rLarge; color: Theme.surface3; border.color: Theme.border }
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: app.reduceMotion ? 0 : 200 } }
        function openFor(id) { editId = id; confirming = false
            var x = id ? app.cardDetail(id) : {}
            kTitle.text = x.title || ""; kHolder.text = x.cardholder || ""; kExp.text = x.expiry || ""; kNotes.text = x.notes || ""
            sNum.reset(); sCvv.reset(); open(); kTitle.forceActiveFocus() }
        onClosed: { sNum.reset(); sCvv.reset() }
        contentItem: ColumnLayout { spacing: 10
            Text { text: k.editId ? "Edit card" : "Add card"; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
            Field { id: kTitle; Layout.fillWidth: true; placeholderText: "Title" }
            Field { id: kHolder; Layout.fillWidth: true; placeholderText: "Cardholder" }
            CardSecret { id: sNum; Layout.fillWidth: true; cardId: k.editId; field: "number"; label: "Card number" }
            RowLayout { Layout.fillWidth: true; spacing: 10
                Field { id: kExp; Layout.preferredWidth: 130; placeholderText: "Expiry (MM/YY)" }
                CardSecret { id: sCvv; Layout.fillWidth: true; cardId: k.editId; field: "cvv"; label: "Security code" } }
            TextArea { id: kNotes; Layout.fillWidth: true; Layout.preferredHeight: 60; placeholderText: "Notes"; wrapMode: TextEdit.Wrap
                color: Theme.text; placeholderTextColor: Theme.muted; leftPadding: 12; topPadding: 8
                background: Rectangle { radius: Theme.rSmall; color: Theme.surface2; border.width: parent.activeFocus ? 2 : 1; border.color: parent.activeFocus ? Theme.accent : Theme.borderStrong } }
            Text { visible: app.error !== ""; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.dangerText; text: app.error }
            Text { visible: k.confirming; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.text
                text: "Delete \"" + kTitle.text + "\"? The card details are removed from this device and can't be recovered." }
            RowLayout { Layout.fillWidth: true
                GhostButton { visible: k.editId !== 0 && !k.confirming; text: "Delete"; onClicked: k.confirming = true }
                Button { visible: k.confirming; text: "Delete"; implicitHeight: 40; onClicked: { if (app.removeCard(k.editId)) { k.close(); win.toast("Deleted") } }
                    contentItem: Text { text: parent.text; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { radius: Theme.rSmall; color: Theme.dangerFill } }
                GhostButton { visible: k.confirming; text: "Keep"; onClicked: k.confirming = false }
                Item { Layout.fillWidth: true }
                GhostButton { text: "Cancel"; onClicked: k.close() }
                PrimaryButton { text: "Save"; enabled: !k.confirming
                    onClicked: if (app.saveCard({id: k.editId, title: kTitle.text, cardholder: kHolder.text, expiry: kExp.text, notes: kNotes.text,
                        number: sNum.changing ? sNum.value : "", cvv: sCvv.changing ? sCvv.value : ""})) { k.close(); win.toast("Saved") } } }
        }
    }

    component Icon: Item {
        property string name: ""
        property color tint: Theme.text2
        property int size: 18
        implicitWidth: size; implicitHeight: size
        Image { id: im; anchors.fill: parent; source: "qrc:/assets/icons/" + parent.name + ".svg"; sourceSize: Qt.size(parent.size * 2, parent.size * 2); visible: false }
        MultiEffect { anchors.fill: parent; source: im; colorization: 1.0; colorizationColor: parent.tint }
    }

    // Shield with a keyhole cut from negative space. One colour, reads at 16 px.
    component Mark: Canvas {
        property color tint: Theme.accent
        implicitWidth: 28; implicitHeight: 28
        onTintChanged: requestPaint()
        onPaint: { var c = getContext("2d"), w = width, h = height; c.reset(); c.fillStyle = tint
            c.beginPath(); c.moveTo(w/2, 0); c.lineTo(w, h*0.2); c.lineTo(w, h*0.55); c.quadraticCurveTo(w, h*0.85, w/2, h)
            c.quadraticCurveTo(0, h*0.85, 0, h*0.55); c.lineTo(0, h*0.2); c.closePath(); c.fill()
            c.globalCompositeOperation = "destination-out"
            c.beginPath(); c.arc(w/2, h*0.42, w*0.15, 0, 6.2832); c.fill()
            c.beginPath(); c.moveTo(w*0.45, h*0.5); c.lineTo(w*0.55, h*0.5); c.lineTo(w*0.6, h*0.72); c.lineTo(w*0.4, h*0.72); c.closePath(); c.fill() }
    }

    // ---------- state switch: the shell is unloaded whenever the vault is not open ----------
    Connections { target: app; function onNotice(t) { win.toast(t) } }
    Loader {
        anchors.fill: parent
        sourceComponent: app.state === "unlocked" ? shell : app.state === "unreadable" ? unreadable : auth
    }

    // ---------- sign in / create account / unlock ----------
    Component { id: auth
        Rectangle { color: Theme.bg; opacity: 0
            Component.onCompleted: opacity = 1
            Behavior on opacity { NumberAnimation { duration: app.reduceMotion ? 0 : 400 } }   // calm fade, never an error screen
            property bool first: app.state === "firstRun"
            property var pol: app.policy(pw.text)
            ColumnLayout { anchors.centerIn: parent; width: 360; spacing: 14
                Mark { Layout.alignment: Qt.AlignHCenter; implicitWidth: 44; implicitHeight: 44 }
                Text { Layout.alignment: Qt.AlignHCenter; color: Theme.text; font.pixelSize: 20; font.weight: Font.DemiBold
                    text: first ? "Create your vault" : app.lockReason !== "" ? "Vault locked" : "Sign in" }
                Text { Layout.alignment: Qt.AlignHCenter; color: Theme.text2; text: app.lockReason !== "" ? app.lockReason : "Stored only on this device." }
                Text { visible: app.discarded; Layout.alignment: Qt.AlignHCenter; color: Theme.warning; text: "Unsaved changes were discarded when the vault locked." }
                Field { id: user; Layout.fillWidth: true; placeholderText: "Username"; text: app.username; onAccepted: pw.forceActiveFocus() }
                SecretField { id: pw; Layout.fillWidth: true; placeholderText: "Master password"; onAccepted: go.clicked() }
                Column { visible: first; Layout.fillWidth: true; spacing: 4
                    Text { color: pol.lengthOk ? Theme.success : Theme.muted; text: (pol.lengthOk ? "Done: " : "Needed: ") + "at least 12 characters" }
                    Text { color: pol.notWeak ? Theme.success : Theme.muted; text: (pol.notWeak ? "Done: " : "Needed: ") + "not a common or simple pattern" }
                }
                CheckBox { id: ack; visible: first; Layout.fillWidth: true; palette.windowText: Theme.text2
                    text: "I understand a forgotten master password cannot be recovered." }
                Text { visible: app.error !== ""; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.dangerText; text: app.error }
                PrimaryButton { id: go; Layout.fillWidth: true; enabled: !app.busy && user.text.length > 0 && pw.text.length > 0 && (!first || (pol.ok && ack.checked))
                    text: app.busy ? "Working…" : first ? "Create vault" : app.lockReason !== "" ? "Unlock" : "Sign in"
                    onClicked: { if (first) app.createAccount(user.text, pw.text, ack.checked); else app.signIn(user.text, pw.text); pw.clear() } }
            }
        }
    }

    // ---------- vault unreadable (§9.3): no write path exists in this view ----------
    Component { id: unreadable
        Rectangle { color: Theme.bg
            ColumnLayout { anchors.centerIn: parent; width: 440; spacing: 12
                Text { color: Theme.dangerText; font.pixelSize: 20; font.weight: Font.DemiBold; text: "Vault unreadable" }
                Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.text2
                    text: "Your master password is right, but the vault file could not be read. SecureVault has not changed it. Check that this is the correct data folder, or restore from a backup you made earlier:" }
                Text { Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; color: Theme.text; font.family: Theme.mono; text: app.dataDir }
                GhostButton { text: "Lock"; onClicked: app.lock() }
            }
        }
    }

    // ---------- shell ----------
    Component { id: shell
        RowLayout { id: shellRoot; spacing: 0
            property int page: 0
            property var icons: ["layout-grid", "key-round", "file-text", "credit-card", "star", "wand-sparkles", "shield-check", "activity", "settings"]
            CredDialog { id: cred }
            NoteDialog { id: note }
            CardDialog { id: card }
            Shortcut { sequence: "Ctrl+K"; onActivated: pal.open() }
            // Command palette: pages, actions and item titles. Never shows or searches secrets.
            Popup { id: pal; modal: true; focus: true; width: 520; padding: 12; anchors.centerIn: Overlay.overlay
                property var results: []
                property var pages: ["Dashboard", "Passwords", "Secure notes", "Cards", "Favorites", "Generator", "Security center", "Activity log", "Settings"]
                background: Rectangle { radius: Theme.rLarge; color: Theme.surface3; border.color: Theme.border }
                function refresh() { var t = pq.text.trim().toLowerCase(), r = []
                    for (var i = 0; i < pages.length; i++) if (t === "" || pages[i].toLowerCase().indexOf(t) >= 0) r.push({label: "Go to " + pages[i], hint: "", kind: "page", id: i})
                    var acts = [["Add password", "addpw"], ["Add note", "addnote"], ["Add card", "addcard"], ["Lock vault", "lock"]]
                    for (var j = 0; j < acts.length; j++) if (t === "" || acts[j][0].toLowerCase().indexOf(t) >= 0) r.push({label: acts[j][0], hint: "", kind: acts[j][1], id: 0})
                    results = r.concat(app.paletteItems(t)); pl.currentIndex = 0 }
                function run(it) { if (!it) return; close()
                    if (it.kind === "page") shellRoot.page = it.id
                    else if (it.kind === "lock") app.lock()
                    else if (it.kind === "addpw") { shellRoot.page = 1; cred.openFor(0) }
                    else if (it.kind === "addnote") { shellRoot.page = 2; note.openFor(0) }
                    else if (it.kind === "addcard") { shellRoot.page = 3; card.openFor(0) }
                    else if (it.kind === "password") { shellRoot.page = 1; cred.openFor(it.id) }
                    else if (it.kind === "note") { shellRoot.page = 2; note.openFor(it.id) }
                    else if (it.kind === "card") { shellRoot.page = 3; card.openFor(it.id) } }
                onOpened: { pq.clear(); refresh(); pq.forceActiveFocus() }
                contentItem: ColumnLayout { spacing: 8
                    Field { id: pq; Layout.fillWidth: true; placeholderText: "Search pages, actions and items"; onTextChanged: pal.refresh()
                        Keys.onDownPressed: pl.incrementCurrentIndex(); Keys.onUpPressed: pl.decrementCurrentIndex()
                        onAccepted: pal.run(pal.results[pl.currentIndex]) }
                    ListView { id: pl; Layout.fillWidth: true; Layout.preferredHeight: Math.min(contentHeight, 320); clip: true; model: pal.results; currentIndex: 0
                        delegate: Rectangle { width: pl.width; height: 36; radius: Theme.rSmall; color: ListView.isCurrentItem ? Theme.surface2 : "transparent"
                            RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10
                                Text { Layout.fillWidth: true; text: modelData.label; color: Theme.text; elide: Text.ElideRight }
                                Text { text: modelData.hint; color: Theme.muted } }
                            TapHandler { onTapped: pal.run(modelData) } } } }
            }

            Rectangle { Layout.fillHeight: true; Layout.preferredWidth: 220; color: Theme.surface1
                ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 4
                    RowLayout { Layout.bottomMargin: 16; spacing: 8; Mark { implicitWidth: 22; implicitHeight: 22 }
                        Text { text: "SecureVault"; color: Theme.text; font.weight: Font.DemiBold; font.pixelSize: 14 } }
                    Repeater { model: ["Dashboard", "Passwords", "Secure notes", "Cards", "Favorites", "Generator", "Security center", "Activity log", "Settings"]
                        delegate: ItemDelegate { id: dlg; Layout.fillWidth: true; text: modelData; onClicked: page = index
                            contentItem: RowLayout { spacing: 10
                                Icon { Layout.leftMargin: 6; name: shellRoot.icons[index]; tint: page === index ? Theme.accentText : Theme.text2 }
                                Text { Layout.fillWidth: true; text: dlg.text; color: page === index ? Theme.accentText : Theme.text2; verticalAlignment: Text.AlignVCenter } }
                            background: Rectangle { radius: Theme.rSmall; color: page === index ? Theme.surface3 : parent.hovered ? Theme.surface2 : "transparent"
                                Rectangle { width: 3; height: parent.height * 0.5; anchors.verticalCenter: parent.verticalCenter; radius: 2; color: Theme.accent; visible: page === index } } } }
                    Item { Layout.fillHeight: true }
                    Text { text: "Ctrl+K search, Ctrl+L lock"; color: Theme.muted; font.pixelSize: 12 }
                    Text { text: app.username; color: Theme.text; font.weight: Font.Medium }
                    Text { text: app.score + " out of 100, " + app.band; color: Theme.bandColor(app.band) }
                    PrimaryButton { Layout.fillWidth: true; Layout.topMargin: 8; text: "Lock vault"; onClicked: app.lock(); Accessible.name: "Lock vault" }
                }
            }
            StackLayout { Layout.fillWidth: true; Layout.fillHeight: true; currentIndex: page
                // Dashboard: score ring, recommendations from the analyzer's counts, recent activity with details masked.
                Item {
                    RowLayout { anchors.centerIn: parent; spacing: 56
                        ColumnLayout { spacing: 14
                            Item { Layout.alignment: Qt.AlignHCenter; width: 240; height: 240
                                property real shown: 0
                                Component.onCompleted: ringAnim.start()
                                NumberAnimation on shown { id: ringAnim; running: false; from: 0; to: app.score; duration: app.reduceMotion ? 0 : 700; easing.type: Easing.OutCubic }
                                onShownChanged: ring.requestPaint()
                                Canvas { id: ring; anchors.fill: parent
                                    onPaint: { var c = getContext("2d"); c.reset(); c.lineWidth = 12; c.lineCap = "round"
                                        c.strokeStyle = Theme.surface3; c.beginPath(); c.arc(120, 120, 104, 0, 6.2832); c.stroke()
                                        c.strokeStyle = Theme.bandColor(app.band); c.beginPath()
                                        c.arc(120, 120, 104, -1.5708, -1.5708 + 6.2832 * parent.shown / 100); c.stroke() } }
                                Column { anchors.centerIn: parent
                                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: app.score; color: Theme.text; font.pixelSize: 56; font.weight: Font.DemiBold }
                                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: app.band; color: Theme.bandColor(app.band) } } }
                            Text { Layout.alignment: Qt.AlignHCenter; color: Theme.text2
                                text: app.total === 0 ? "No passwords saved yet." : app.attention === 0 ? "All " + app.total + " passwords look healthy." : app.attention + " of " + app.total + " passwords need attention." }
                        }
                        ColumnLayout { Layout.preferredWidth: 360; spacing: 8
                            Text { text: "What to do next"; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                            Repeater {
                                model: { var r = []
                                    if (app.total === 0) r.push({t: "Add your first password", p: 1})
                                    if (app.reusedCount > 0) r.push({t: "Replace " + app.reusedCount + " reused " + (app.reusedCount === 1 ? "password" : "passwords"), p: 6})
                                    if (app.weakCount > 0) r.push({t: "Strengthen " + app.weakCount + " weak " + (app.weakCount === 1 ? "password" : "passwords"), p: 6})
                                    if (app.oldCount > 0) r.push({t: "Update " + app.oldCount + " " + (app.oldCount === 1 ? "password" : "passwords") + " older than 180 days", p: 6})
                                    return r }
                                delegate: RowLayout { Layout.fillWidth: true
                                    Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.text; text: modelData.t }
                                    GhostButton { text: "Review"; onClicked: shellRoot.page = modelData.p } } }
                            Text { visible: app.total > 0 && app.attention === 0; color: Theme.success; text: "Nothing needs your attention." }
                            Text { Layout.topMargin: 20; text: "Recent activity"; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                            Repeater { model: { app.rev; app.score; return app.activity().slice(0, 5) }
                                delegate: RowLayout { Layout.fillWidth: true; spacing: 12
                                    Text { Layout.preferredWidth: 130; color: Theme.text2; text: modelData.time }
                                    Text { Layout.fillWidth: true; color: Theme.text; elide: Text.ElideRight; text: al.names[modelData.action] || modelData.action } } }   // details are left out on purpose
                        }
                    }
                }
                // Passwords: dense rows, summaries only. No reveal in this build.
                ColumnLayout { anchors.margins: 24; spacing: 12
                    RowLayout { Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.topMargin: 24
                        Text { Layout.fillWidth: true; text: "Passwords"; color: Theme.text; font.pixelSize: 20; font.weight: Font.DemiBold }
                        PrimaryButton { text: "Add password"; onClicked: cred.openFor(0) } }
                    Field { Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; placeholderText: "Search title, website, username"; onTextChanged: app.search(text) }
                    ListView { id: list; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: app.credentials
                        Text { visible: list.count === 0; anchors.centerIn: parent; color: Theme.muted; text: app.total === 0 ? "No passwords yet. Add your first one." : "Nothing matches your search." }
                        delegate: Rectangle { width: list.width; height: 52; color: hov.hovered ? Theme.surface2 : "transparent"
                            HoverHandler { id: hov }
                            RowLayout { anchors.fill: parent; anchors.leftMargin: 24; anchors.rightMargin: 24; spacing: 12
                                Rectangle { width: 32; height: 32; radius: Theme.rSmall; color: Qt.hsla((title.charCodeAt(0) * 47 % 360) / 360, 0.35, 0.28, 1)
                                    Text { anchors.centerIn: parent; text: title.charAt(0).toUpperCase(); color: Theme.text; font.weight: Font.DemiBold } }
                                Column { Layout.fillWidth: true
                                    Text { text: title; color: Theme.text; font.weight: Font.Medium; elide: Text.ElideRight; width: parent.width }
                                    Text { text: (site !== "" ? site + ", " : "") + username; color: Theme.text2; elide: Text.ElideRight; width: parent.width } }
                                Text { visible: isReused; text: "Reused"; color: Theme.warning }
                                Text { visible: isOld; text: "Old"; color: Theme.warning }
                                Text { text: strength; color: Theme.strengthColor(strength); font.weight: Font.Medium }
                                GhostButton { text: favorite ? "Unstar" : "Star"; onClicked: app.setFavorite(id, !favorite) }
                                GhostButton { text: "Edit"; onClicked: cred.openFor(id) }
                                GhostButton { text: "Copy"; onClicked: win.toast(app.copyPassword(id) ? "Password copied" : "Copying isn't available on this platform.") }
                            } }
                    }
                }
                // Secure notes: titles in the list, content only inside the editor.
                ColumnLayout { spacing: 12
                    RowLayout { Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.topMargin: 24
                        Text { Layout.fillWidth: true; text: "Secure notes"; color: Theme.text; font.pixelSize: 20; font.weight: Font.DemiBold }
                        PrimaryButton { text: "Add note"; onClicked: note.openFor(0) } }
                    Field { id: nq; Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; placeholderText: "Search title, category, content" }
                    ListView { id: nl; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                        model: { app.rev; return app.notes(nq.text) }
                        Text { visible: nl.count === 0; anchors.centerIn: parent; color: Theme.muted; text: nq.text === "" ? "No notes yet. Add your first one." : "Nothing matches your search." }
                        delegate: Rectangle { width: nl.width; height: 48; color: nh.hovered ? Theme.surface2 : "transparent"
                            HoverHandler { id: nh }
                            RowLayout { anchors.fill: parent; anchors.leftMargin: 24; anchors.rightMargin: 24; spacing: 12
                                Text { Layout.fillWidth: true; text: modelData.title; color: Theme.text; font.weight: Font.Medium; elide: Text.ElideRight }
                                Text { text: modelData.category; color: Theme.text2 }
                                GhostButton { text: modelData.favorite ? "Unstar" : "Star"; onClicked: app.setFavoriteOf("note", modelData.id, !modelData.favorite) }
                                GhostButton { text: "Open"; onClicked: note.openFor(modelData.id) } } } }
                }

                // Cards: dense rows, number masked to its last four digits. Brand is shown, never stored.
                ColumnLayout { spacing: 12
                    RowLayout { Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.topMargin: 24
                        Text { Layout.fillWidth: true; text: "Cards"; color: Theme.text; font.pixelSize: 20; font.weight: Font.DemiBold }
                        PrimaryButton { text: "Add card"; onClicked: card.openFor(0) } }
                    Field { id: cq; Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; placeholderText: "Search title, cardholder, notes" }
                    ListView { id: cl; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                        model: { app.rev; return app.cards(cq.text) }
                        Text { visible: cl.count === 0; anchors.centerIn: parent; color: Theme.muted; text: cq.text === "" ? "No cards yet. Add your first one." : "Nothing matches your search." }
                        delegate: Rectangle { width: cl.width; height: 52; color: ch.hovered ? Theme.surface2 : "transparent"
                            HoverHandler { id: ch }
                            RowLayout { anchors.fill: parent; anchors.leftMargin: 24; anchors.rightMargin: 24; spacing: 12
                                Column { Layout.fillWidth: true
                                    Text { text: modelData.title; color: Theme.text; font.weight: Font.Medium; elide: Text.ElideRight; width: parent.width }
                                    Text { text: modelData.cardholder; color: Theme.text2; elide: Text.ElideRight; width: parent.width } }
                                Text { text: modelData.brand; color: Theme.text2 }
                                Text { text: "•••• " + modelData.last4; color: Theme.text; font.family: Theme.mono }
                                Text { text: modelData.expiry; color: Theme.text2 }
                                GhostButton { text: modelData.favorite ? "Unstar" : "Star"; onClicked: app.setFavoriteOf("card", modelData.id, !modelData.favorite) }
                                GhostButton { text: "Open"; onClicked: card.openFor(modelData.id) } } } }
                }

                // Favorites: starred passwords, notes and cards in one list. Titles only.
                ColumnLayout { spacing: 12
                    Text { Layout.leftMargin: 24; Layout.topMargin: 24; text: "Favorites"; color: Theme.text; font.pixelSize: 20; font.weight: Font.DemiBold }
                    ListView { id: fl; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                        model: { app.rev; return app.favorites() }
                        Text { visible: fl.count === 0; anchors.centerIn: parent; color: Theme.muted; text: "Nothing starred yet. Star an item to find it here." }
                        delegate: Rectangle { width: fl.width; height: 48; color: fh.hovered ? Theme.surface2 : "transparent"
                            HoverHandler { id: fh }
                            RowLayout { anchors.fill: parent; anchors.leftMargin: 24; anchors.rightMargin: 24; spacing: 12
                                Text { Layout.fillWidth: true; text: modelData.title; color: Theme.text; font.weight: Font.Medium; elide: Text.ElideRight }
                                Text { text: modelData.kind === "password" ? "Password" : modelData.kind === "note" ? "Secure note" : "Card"; color: Theme.text2 }
                                Text { text: modelData.hint; color: Theme.text2 }
                                GhostButton { text: "Unstar"; onClicked: app.setFavoriteOf(modelData.kind, modelData.id, false) }
                                GhostButton { text: "Open"; onClicked: { if (modelData.kind === "password") cred.openFor(modelData.id); else if (modelData.kind === "note") note.openFor(modelData.id); else card.openFor(modelData.id) } } } } }
                }

                // Generator: random characters or a passphrase. Exact bits; no score for passphrases (§5).
                ColumnLayout { id: gen; spacing: 10; Layout.margins: 24
                    property bool phrase: false
                    property string out: ""
                    property int bits: 0
                    property bool failed: false
                    function paint(s) { var r = ""
                        for (var i = 0; i < s.length; i++) { var ch = s[i]
                            var e = ch === "<" ? "&lt;" : ch === ">" ? "&gt;" : ch === "&" ? "&amp;" : ch
                            if (/[0-9]/.test(ch)) r += "<font color='" + Theme.accentText + "'>" + e + "</font>"
                            else if (!phrase && /[^A-Za-z0-9]/.test(ch)) r += "<font color='" + Theme.warning + "'>" + e + "</font>"
                            else r += e }
                        return r }
                    function run() { var g = phrase ? app.generatePassphrase(words.value) : app.generate(len.value, cU.checked, cL.checked, cD.checked, cS.checked, cA.checked)
                        failed = !g.ok; out = g.ok ? g.text : ""; bits = g.ok ? g.bits : 0 }
                    Text { text: "Generator"; color: Theme.text; font.pixelSize: 20; font.weight: Font.DemiBold }
                    RowLayout { GhostButton { text: "Random characters"; font.weight: gen.phrase ? Font.Normal : Font.DemiBold; onClicked: { gen.phrase = false; gen.out = "" } }
                        GhostButton { text: "Passphrase"; font.weight: gen.phrase ? Font.DemiBold : Font.Normal; onClicked: { gen.phrase = true; gen.out = "" } } }
                    RowLayout { visible: !gen.phrase; Text { color: Theme.text2; text: "Length " + len.value }
                        Slider { id: len; from: 8; to: 128; stepSize: 1; value: 20; Layout.preferredWidth: 260 } }
                    Flow { visible: !gen.phrase; Layout.preferredWidth: 520; spacing: 12
                        CheckBox { id: cU; checked: true; text: "Uppercase"; palette.windowText: Theme.text2 }
                        CheckBox { id: cL; checked: true; text: "Lowercase"; palette.windowText: Theme.text2 }
                        CheckBox { id: cD; checked: true; text: "Digits"; palette.windowText: Theme.text2 }
                        CheckBox { id: cS; checked: true; text: "Symbols"; palette.windowText: Theme.text2 }
                        CheckBox { id: cA; checked: true; text: "Avoid look-alike characters"; palette.windowText: Theme.text2 } }
                    RowLayout { visible: gen.phrase; Text { color: Theme.text2; text: "Words " + words.value }
                        Slider { id: words; from: 3; to: 12; stepSize: 1; value: 5; Layout.preferredWidth: 260 } }
                    PrimaryButton { text: "Generate"; onClicked: gen.run() }
                    Text { visible: gen.failed; color: Theme.dangerText; text: "Pick at least one character type." }
                    Rectangle { visible: gen.out !== ""; Layout.preferredWidth: 520; implicitHeight: outText.implicitHeight + 24; radius: Theme.rMed; color: Theme.surface2; border.color: Theme.border
                        Text { id: outText; anchors.fill: parent; anchors.margins: 12; wrapMode: Text.WrapAnywhere; textFormat: Text.StyledText
                            font.family: Theme.mono; font.pixelSize: 16; color: Theme.text; text: gen.paint(gen.out) } }
                    Text { visible: gen.out !== ""; color: Theme.text2
                        text: gen.phrase ? words.value + " random words from a list of 2048, about " + gen.bits + " bits."
                                         : "Random characters, about " + gen.bits + " bits. Rated " + app.assess(gen.out).label + "." }
                    GhostButton { visible: gen.out !== ""; text: "Copy"; onClicked: win.toast(app.copyGenerated() ? "Copied" : "Copying isn't available on this platform.") }
                    Item { Layout.fillHeight: true }
                }
                // Security center: the analyzer's numbers and per-credential flags, nothing recomputed here.
                ColumnLayout { spacing: 8; Layout.margins: 24
                    Text { text: "Security center"; color: Theme.text; font.pixelSize: 20; font.weight: Font.DemiBold }
                    Text { color: Theme.bandColor(app.band); text: app.score + " out of 100, " + app.band }
                    Text { color: Theme.text2; text: app.weakCount + " weak, " + app.reusedCount + " reused, " + app.oldCount + " not changed in over 180 days" }
                    Text { visible: app.attention === 0; color: Theme.text2; text: app.total === 0 ? "Add passwords to see how healthy they are." : "Nothing needs attention." }
                    ListView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: app.credentials
                        delegate: Rectangle { visible: isWeak || isReused || isOld; width: ListView.view.width; height: visible ? 48 : 0; color: "transparent"
                            RowLayout { anchors.fill: parent; spacing: 12
                                Text { Layout.fillWidth: true; color: Theme.text; elide: Text.ElideRight; text: title }
                                Text { color: Theme.warning; text: [isWeak ? "Weak" : "", isReused ? "Reused" : "", isOld ? "Old" : ""].filter(function (x) { return x !== "" }).join(", ") }
                                GhostButton { text: "Edit"; onClicked: cred.openFor(id) } } } }
                }

                // Activity log: plain text on disk, never contains passwords.
                ColumnLayout { spacing: 8
                    Text { Layout.leftMargin: 24; Layout.topMargin: 24; text: "Activity log"; color: Theme.text; font.pixelSize: 20; font.weight: Font.DemiBold }
                    Text { Layout.leftMargin: 24; Layout.preferredWidth: 520; wrapMode: Text.Wrap; color: Theme.text2
                        text: "A plain-text record of what happened in this vault. It never records passwords." }
                    ListView { id: al; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: app.state === "unlocked" ? app.activity() : []
                        property var names: ({LOGIN_SUCCESS: "Signed in", LOGIN_FAILED: "Failed sign-in", VAULT_UNLOCK: "Unlocked", LOCK_VAULT: "Locked", AUTO_LOCK: "Locked after inactivity",
                            LOGOUT: "Signed out", ADD_ENTRY: "Added password", UPDATE_ENTRY: "Changed password entry", DELETE_ENTRY: "Deleted password", COPY_PASSWORD: "Copied password",
                            REVEAL_PASSWORD: "Revealed password", ADD_NOTE: "Added note", UPDATE_NOTE: "Changed note", DELETE_NOTE: "Deleted note", ADD_CARD: "Added card",
                            UPDATE_CARD: "Changed card", DELETE_CARD: "Deleted card", REVEAL_CARD: "Revealed card", BACKUP: "Backed up vault", RESTORE: "Restored vault",
                            CHANGE_MASTER_PASSWORD: "Changed master password"})
                        delegate: RowLayout { width: al.width; height: 40; spacing: 12
                            Text { Layout.leftMargin: 24; Layout.preferredWidth: 150; text: modelData.time; color: Theme.text2 }
                            Text { Layout.preferredWidth: 200; text: al.names[modelData.action] || modelData.action; color: Theme.text; font.weight: Font.Medium }
                            Text { Layout.fillWidth: true; Layout.rightMargin: 24; text: modelData.detail; color: Theme.text2; elide: Text.ElideRight } } }
                }

                // Settings: facts come from the real backend. Backup is a copy of the ENCRYPTED file.
                ColumnLayout { id: set; spacing: 10; property url picked: ""
                    Layout.margins: 24
                    FileDialog { id: saveDlg; fileMode: FileDialog.SaveFile; nameFilters: ["SecureVault backup (*.sv)"]; defaultSuffix: "sv"; onAccepted: app.backupTo(selectedFile) }
                    FileDialog { id: openDlg; fileMode: FileDialog.OpenFile; nameFilters: ["SecureVault backup (*.sv)"]; onAccepted: set.picked = selectedFile }
                    Text { text: "Settings"; color: Theme.text; font.pixelSize: 20; font.weight: Font.DemiBold }
                    Text { color: Theme.text2; text: "Version " + app.version }
                    Text { color: Theme.text2; text: "Vault encrypted with AES-256-GCM. Key derived with PBKDF2-HMAC-SHA256, " + app.kdfIterations + " iterations." }
                    Text { color: Theme.text2; text: "Your master password is never stored. Everything stays on this device." }
                    Text { color: Theme.text2; text: "Data folder" }
                    Text { Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; color: Theme.text; font.family: Theme.mono; text: app.dataDir }


                    Text { Layout.topMargin: 16; text: "Appearance"; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                    RowLayout { Text { color: Theme.text2; text: "Animations" }
                        ComboBox { id: motionBox; model: ["Follow system setting", "Reduce", "Full"]
                            currentIndex: app.motionMode === "reduce" ? 1 : app.motionMode === "full" ? 2 : 0
                            onActivated: app.setMotionMode(["system", "reduce", "full"][currentIndex]) } }
                    Text { Layout.topMargin: 16; text: "Third-party licences"; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                    Text { Layout.preferredWidth: 520; wrapMode: Text.Wrap; color: Theme.text2
                        text: "Qt (LGPL, dynamically linked). Inter and JetBrains Mono fonts (SIL Open Font License 1.1). Lucide icons (ISC). BIP-39 English word list (MIT). Licence texts are in the licenses folder next to the source." }
                    Text { Layout.topMargin: 16; text: "Master password"; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                    GhostButton { visible: !mpBox.visible; text: "Change master password"; onClicked: { app.clearError(); mpBox.visible = true } }
                    ColumnLayout { id: mpBox; visible: false; Layout.preferredWidth: 520; spacing: 8
                        property var pol: app.policy(mpNew.text)
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.text2
                            text: "Your vault is re-encrypted with the new password. Backups made before this can't be restored." }
                        SecretField { id: mpOld; Layout.fillWidth: true; placeholderText: "Current master password" }
                        SecretField { id: mpNew; Layout.fillWidth: true; placeholderText: "New master password" }
                        Text { color: mpBox.pol.lengthOk ? Theme.success : Theme.muted; text: (mpBox.pol.lengthOk ? "Done: " : "Needed: ") + "at least 12 characters" }
                        Text { color: mpBox.pol.notWeak ? Theme.success : Theme.muted; text: (mpBox.pol.notWeak ? "Done: " : "Needed: ") + "not a common or simple pattern" }
                        CheckBox { id: mpAck; palette.windowText: Theme.text2; text: "I understand a forgotten master password cannot be recovered." }
                        ProgressBar { visible: app.progress >= 0; Layout.fillWidth: true; from: 0; to: 100; value: app.progress }
                        Text { visible: app.error !== ""; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.dangerText; text: app.error }
                        RowLayout {
                            Button { text: app.busy ? "Working…" : "Change master password"; implicitHeight: 40
                                enabled: !app.busy && mpOld.text.length > 0 && mpBox.pol.ok && mpAck.checked
                                onClicked: { app.changeMasterPassword(mpOld.text, mpNew.text, mpAck.checked); mpOld.clear(); mpNew.clear() }
                                contentItem: Text { text: parent.text; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle { radius: Theme.rSmall; color: parent.enabled ? Theme.accentFill : Theme.surface3 } }
                            GhostButton { enabled: !app.busy; text: "Cancel"; onClicked: { mpOld.clear(); mpNew.clear(); mpAck.checked = false; mpBox.visible = false } } } }
                    Text { Layout.topMargin: 16; text: "Backup and restore"; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                    Text { Layout.preferredWidth: 520; wrapMode: Text.Wrap; color: Theme.text2
                        text: "A backup is a copy of your encrypted vault. Backups made before you change your master password can't be restored. If a restore fails, SecureVault keeps your current vault." }
                    RowLayout { PrimaryButton { text: "Back up vault"; onClicked: saveDlg.open() }
                        GhostButton { text: "Restore from backup"; onClicked: openDlg.open() } }
                    ColumnLayout { visible: set.picked.toString() !== ""; Layout.preferredWidth: 520; spacing: 8
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.text
                            text: "Restore vault replaces what is in your vault now. Anything added after this backup was made is lost." }
                        SecretField { id: rpw; Layout.fillWidth: true; placeholderText: "Current master password" }
                        RowLayout {
                            Button { text: "Restore vault"; implicitHeight: 40; enabled: !app.busy && rpw.text.length > 0
                                onClicked: { app.restoreFrom(set.picked, rpw.text); rpw.clear(); set.picked = "" }
                                contentItem: Text { text: parent.text; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle { radius: Theme.rSmall; color: parent.enabled ? Theme.dangerFill : Theme.surface3 } }
                            GhostButton { text: "Cancel"; onClicked: { rpw.clear(); set.picked = "" } } } }
                    Item { Layout.fillHeight: true }
                }

            }
        }
    }

    // ---------- quiet countdown (last 60 s) and toast ----------
    Rectangle { visible: app.state === "unlocked" && app.secondsLeft >= 0; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 16
        radius: Theme.rMed; color: Theme.surface3; border.color: Theme.border; width: cd.width + 24; height: 40
        Row { id: cd; anchors.centerIn: parent; spacing: 12
            Text { text: "Locking in " + app.secondsLeft + " s"; color: Theme.text2; anchors.verticalCenter: parent.verticalCenter }
            GhostButton { text: "Stay unlocked"; onClicked: app.stayUnlocked() } } }
    Rectangle { visible: toastText !== ""; anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; anchors.margins: 24
        radius: Theme.rMed; color: Theme.surface3; border.color: Theme.border; width: tt.width + 32; height: 40
        Text { id: tt; anchors.centerIn: parent; text: toastText; color: Theme.text } }
}
