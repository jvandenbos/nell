pragma Singleton
import QtQuick

// The colors every surface is drawn from. The reader's panels take theirs
// from the reading theme, so a drawer on Paper is paper rather than a slab of
// dark gray; the library takes its from the desktop's Omarchy theme. Secondary
// text, focus rings, and accent-colored text are computed to stay readable on
// whichever surface they land on, instead of being faded with opacity.
QtObject {
    id: tokens

    readonly property bool reading: reader.open
    readonly property color base: reading ? prefs.pageColor : theme.background
    readonly property color ink: reading ? prefs.textColor : theme.foreground
    readonly property bool dark: luminance(base) < luminance(ink)

    readonly property color surface: base
    // Panels, popups, and cards: a step off the page toward the ink on a dark
    // page, toward white on a light one, the way paper stacks.
    readonly property color surfaceRaised: dark ? mix(base, ink, 0.07) : mix(base, "#ffffff", 0.6)
    // Wells: segmented controls, progress tracks, a hovered row.
    readonly property color surfaceSunken: mix(base, ink, dark ? 0.12 : 0.07)
    readonly property color hover: mix(surfaceRaised, ink, 0.08)
    readonly property color text: ink
    readonly property color textSecondary: readable(ink, [surface, surfaceRaised, surfaceSunken], 4.5)
    readonly property color border: mix(base, ink, 0.16)
    readonly property color accent: theme.accent
    readonly property color accentForeground: theme.accentForeground
    // The accent as text (a current chapter, a search match): the accent itself
    // when it reads, otherwise pulled toward the ink until it does.
    readonly property color accentText: towardInk(accent, [surface, surfaceRaised], 4.5)
    // Focus outlines are non-text, so 3:1 is the bar (WCAG 1.4.11).
    readonly property color focus: towardInk(accent, [surface, surfaceRaised], 3)
    readonly property color danger: towardInk("#e5484d", [surface, surfaceRaised], 4.5)
    readonly property color bookmark: "#e5484d"
    readonly property color scrim: Qt.rgba(0, 0, 0, dark ? 0.6 : 0.35)
    // A label printed over cover artwork ("PDF"), whatever the theme.
    readonly property color coverBadge: Qt.rgba(0, 0, 0, 0.8)
    readonly property color coverBadgeText: "#ffffff"

    // Highlight colors are semantic — a reader's yellow means what it meant
    // last year — so they stay fixed across themes.
    readonly property var highlights: [
        { key: "yellow", color: "#ffd60a", name: qsTr("Yellow highlight") },
        { key: "green", color: "#5fd068", name: qsTr("Green highlight") },
        { key: "blue", color: "#5ab4fa", name: qsTr("Blue highlight") },
        { key: "pink", color: "#ff6fa5", name: qsTr("Pink highlight") },
        { key: "purple", color: "#b38cff", name: qsTr("Purple highlight") }
    ]
    readonly property color underline: "#ff453a"
    function highlightColor(key) {
        if (key === "underline")
            return underline
        const match = highlights.find(h => h.key === key)
        return match ? match.color : highlights[0].color
    }

    // Motion, or none when the reader asked for less of it.
    readonly property bool reduceMotion: prefs.reduceMotion
    function duration(ms) { return reduceMotion ? 0 : ms }

    // ---- Color arithmetic (WCAG 2 relative luminance) ---------------------
    function channel(c) { return c <= 0.03928 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4) }
    function luminance(c) {
        const k = Qt.color(c)
        return 0.2126 * channel(k.r) + 0.7152 * channel(k.g) + 0.0722 * channel(k.b)
    }
    function contrast(a, b) {
        const la = luminance(a), lb = luminance(b)
        return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05)
    }
    function mix(a, b, t) {
        const x = Qt.color(a), y = Qt.color(b)
        return Qt.rgba(x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, 1)
    }
    function passes(c, backgrounds, ratio) {
        for (const b of backgrounds)
            if (contrast(c, b) < ratio)
                return false
        return true
    }
    // The softest blend of the ink into the page that still reads at `ratio`:
    // secondary, but never faint.
    function readable(fg, backgrounds, ratio) {
        for (let t = 0.6; t > 0; t -= 0.02) {
            const c = mix(fg, base, t)
            if (passes(c, backgrounds, ratio))
                return c
        }
        return fg
    }
    function towardInk(c, backgrounds, ratio) {
        for (let t = 0; t <= 1; t += 0.05) {
            const m = mix(c, ink, t)
            if (passes(m, backgrounds, ratio))
                return m
        }
        return ink
    }
}
