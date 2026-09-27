#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Every GUI file reads Colours2000::xxx directly at paint time (not cached),
// so toggling light mode just overwrites these values in place via
// setLightMode() and triggers one repaint - no per-component plumbing needed.
namespace Colours2000
{
    namespace detail
    {
        // Base (dark) palette - inverted-MPC2000, as designed with the user.
        constexpr juce::uint32 darkVoidBgHex     = 0xff1B2029;
        constexpr juce::uint32 darkPanelHex      = 0xff262C3C;
        constexpr juce::uint32 darkPanelHiHex    = 0xff2E354A;
        constexpr juce::uint32 darkInsetHex      = 0xffC0B0C2;
        constexpr juce::uint32 darkInsetTextHex  = 0xff241E2A;
        constexpr juce::uint32 darkPadHex        = 0xffC9C9C9;
        constexpr juce::uint32 darkPadDarkHex    = 0xffA7A7A7;
        constexpr juce::uint32 darkPadEmptyHex   = 0xffB3B3B3;
        constexpr juce::uint32 darkAccentHex     = 0xff3EBBF1;
        constexpr juce::uint32 darkAccentDimHex  = 0xff1F5A73;
        // Was 0x0D2733 (a near-black navy). `accent` itself inverts from a
        // bright cyan (dark mode) to a dark rust-orange (light mode) - text
        // that had comfortable margin against the cyan (7.07:1) fell to
        // 3.77:1, failing WCAG AA, against the much darker orange. Plain
        // black gives strong margins against both (9.58:1 / 5.12:1,
        // verified) rather than trying to preserve the original navy tint.
        constexpr juce::uint32 darkAccentTextHex = 0xff000000; // text drawn on accent-coloured buttons/tabs
        constexpr juce::uint32 darkRedHex        = 0xffE0645A;
        constexpr juce::uint32 darkTextHex       = 0xffEDEBF2;
        // Was 0x8A90A6 (a blue-gray) - measured contrast against its real
        // usage backgrounds (voidBg, panel) failed WCAG AA (4.5:1) in dark
        // mode and failed worse in light mode (simple RGB-inversion doesn't
        // preserve contrast ratios for non-gray hues the way it does for
        // near-neutral grays). Replaced with a plain gray tuned by actually
        // computing WCAG contrast against both real backgrounds in both
        // modes, not just eyeballed - see the "TESTER" screenshot report
        // this fixed. Kept as light as the constraint allows, to stay
        // visually distinct from the full-contrast `text` colour above.
        constexpr juce::uint32 darkTextMutedHex  = 0xffACACAC;
        constexpr juce::uint32 darkBorderHex     = 0xff3C4258;
        constexpr juce::uint32 darkPadTextHex      = 0xff3A3A3A; // number/sample-name text on the light pad face
        // Was 0x9A9A9A - measured contrast against pad/padEmpty (its real
        // usage backgrounds: pad number, note label, "--" placeholder)
        // failed WCAG AA badly in both modes (as low as 1.34:1 - nearly
        // invisible). Darkened to a value actually verified against both
        // real backgrounds in both modes, not eyeballed.
        constexpr juce::uint32 darkPadTextMutedHex = 0xff434343; // placeholder "--" on an empty pad

        inline juce::Colour invert(juce::uint32 argb)
        {
            const juce::Colour c(argb);
            return juce::Colour((juce::uint8) (255 - c.getRed()),
                                 (juce::uint8) (255 - c.getGreen()),
                                 (juce::uint8) (255 - c.getBlue()));
        }

        inline juce::Colour pick(juce::uint32 darkHex, bool light)
        {
            return light ? invert(darkHex) : juce::Colour(darkHex);
        }
    }

    inline bool isLightMode = false;

    inline juce::Colour voidBg     { detail::darkVoidBgHex };
    inline juce::Colour panel      { detail::darkPanelHex };
    inline juce::Colour panelHi    { detail::darkPanelHiHex };
    inline juce::Colour inset      { detail::darkInsetHex };
    inline juce::Colour insetText  { detail::darkInsetTextHex };
    inline juce::Colour pad        { detail::darkPadHex };
    inline juce::Colour padDark    { detail::darkPadDarkHex };
    inline juce::Colour padEmpty   { detail::darkPadEmptyHex };
    inline juce::Colour accent     { detail::darkAccentHex };
    inline juce::Colour accentDim  { detail::darkAccentDimHex };
    inline juce::Colour accentText { detail::darkAccentTextHex };
    inline juce::Colour red        { detail::darkRedHex };
    inline juce::Colour text       { detail::darkTextHex };
    inline juce::Colour textMuted  { detail::darkTextMutedHex };
    inline juce::Colour border     { detail::darkBorderHex };
    inline juce::Colour padText      { detail::darkPadTextHex };
    inline juce::Colour padTextMuted { detail::darkPadTextMutedHex };

    // Overwrites every colour above in place (inverted-RGB when `enabled`).
    // Call once on toggle, then repaint() the top-level editor - every
    // paint() reads these variables fresh, so nothing else needs updating
    // except LookAndFeel's cached colour IDs (see SamplePadLookAndFeel::
    // applyPalette()).
    inline void setLightMode(bool enabled)
    {
        isLightMode = enabled;
        voidBg     = detail::pick(detail::darkVoidBgHex, enabled);
        panel      = detail::pick(detail::darkPanelHex, enabled);
        panelHi    = detail::pick(detail::darkPanelHiHex, enabled);
        inset      = detail::pick(detail::darkInsetHex, enabled);
        insetText  = detail::pick(detail::darkInsetTextHex, enabled);
        pad        = detail::pick(detail::darkPadHex, enabled);
        padDark    = detail::pick(detail::darkPadDarkHex, enabled);
        padEmpty   = detail::pick(detail::darkPadEmptyHex, enabled);
        accent     = detail::pick(detail::darkAccentHex, enabled);
        accentDim  = detail::pick(detail::darkAccentDimHex, enabled);
        accentText = detail::pick(detail::darkAccentTextHex, enabled);
        red        = detail::pick(detail::darkRedHex, enabled);
        text       = detail::pick(detail::darkTextHex, enabled);
        textMuted  = detail::pick(detail::darkTextMutedHex, enabled);
        border     = detail::pick(detail::darkBorderHex, enabled);
        padText      = detail::pick(detail::darkPadTextHex, enabled);
        padTextMuted = detail::pick(detail::darkPadTextMutedHex, enabled);
    }
}
