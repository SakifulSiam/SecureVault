pragma Singleton
import QtQuick
QtObject {
    readonly property color bg: "#0B0F14"; readonly property color surface1: "#10161D"
    readonly property color surface2: "#151C24"; readonly property color surface3: "#19222C"
    readonly property color border: "#26313D"; readonly property color borderStrong: "#667789"
    readonly property color text: "#F1F5F9"; readonly property color text2: "#94A3B8"; readonly property color muted: "#8190A8"
    readonly property color accent: "#6C63FF"; readonly property color accentFill: "#5B52F0"; readonly property color accentText: "#8B84FF"
    readonly property color success: "#22C55E"; readonly property color warning: "#F59E0B"
    readonly property color dangerText: "#F87171"; readonly property color dangerFill: "#DC2626"
    readonly property color cardVisa: "#1A365D"; readonly property color cardMastercard: "#581C10"
    readonly property color cardAmex: "#064E3B"; readonly property color cardDefault: "#1E293B"
    readonly property color chipColor: "#F59E0B"
    readonly property int rSmall: 6; readonly property int rMed: 10; readonly property int rLarge: 14
    // Inter and JetBrains Mono must be bundled before release; these are fallbacks.
    readonly property string ui: "Inter, Segoe UI, Noto Sans, sans-serif"
    readonly property string mono: "JetBrains Mono, Cascadia Mono, Consolas, monospace"
    function strengthColor(label) { return label === "Weak" ? dangerText : label === "Medium" ? warning : success }
    function strengthScoreColor(score) { return score < 40 ? dangerText : score < 75 ? warning : success }
    function bandColor(b) { return b === "Excellent" || b === "Good" ? success : b === "Needs attention" ? warning : dangerText }
    function cardColor(brand) {
        if (brand === "Visa") return cardVisa
        if (brand === "Mastercard") return cardMastercard
        if (brand === "American Express") return cardAmex
        return cardDefault
    }

}
