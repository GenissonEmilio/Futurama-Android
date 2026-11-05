package com.genisson.futuramarecomp;

/** Host regressions for actual multi-finger gestures, without Android or JUnit. */
public final class TouchControlModelTests {
    private static int checks;
    private static void check(boolean condition, String message) {
        ++checks;
        if (!condition) throw new AssertionError(message);
    }
    private static void near(float actual, float expected, String message) {
        check(Math.abs(actual - expected) < .001f, message + ": " + actual + " != " + expected);
    }
    private static long state(int character, int weapon, int context, boolean playing) {
        return character | ((long) weapon << 8) | ((long) context << 3) | (playing ? 64 : 128);
    }
    private static TouchControlModel model(int character, int weapon, boolean playing) {
        TouchControlModel m = new TouchControlModel();
        m.setState(state(character, weapon, 0, playing));
        m.layout(1920, 1080, 1, 0, 0, 0, 0);
        return m;
    }
    private static int press(TouchControlModel m, int pointer, int button) {
        return m.down(pointer, m.cx[button], m.cy[button]);
    }
    private static void release(TouchControlModel m, int pointer, int button) {
        m.up(pointer, m.cx[button], m.cy[button]);
    }
    private static void gestures() {
        TouchControlModel m = model(TouchControlModel.FRY, 2, true);
        check(m.down(7, m.moveX, m.moveY) == TouchControlModel.MOVE, "movement finger owns joystick");
        check(m.down(31, 1100, 420) == TouchControlModel.LOOK, "noncontiguous pointer owns camera");
        check(press(m, 99, TouchControlModel.A) == TouchControlModel.A, "third finger presses action");
        m.move(7, m.moveX + m.moveRadius * .6f, m.moveY - m.moveRadius * .8f);
        m.move(31, 1124, 407);
        near(m.lx, .6f, "move right is positive");
        near(m.ly, .8f, "move up is positive");
        near(m.lookDx, 24, "camera retains rightward relative displacement");
        near(m.lookDy, -13, "camera retains upward relative displacement");
        check(m.isPressed(TouchControlModel.A), "move and look preserve held action");
        m.lookDx = m.lookDy = 0; // The Android view consumes these deltas each frame.
        m.move(31, 1124, 407);
        m.move(7, m.moveX - m.moveRadius, m.moveY);
        near(m.lookDx, 0, "stationary camera finger adds no drift while moving");
        near(m.lookDy, 0, "stationary camera finger adds no vertical drift");
        near(m.lx, -1, "joystick follows pointer ID after other fingers join");
        check(m.down(123, 1200, 430) == TouchControlModel.NONE, "second camera finger cannot steal ownership");
        m.move(123, 1700, 700);
        near(m.lookDx, 0, "unowned finger adds no camera movement");
        release(m, 99, TouchControlModel.A);
        check(!m.isPressed(TouchControlModel.A) && m.movePointer == 7 && m.lookPointer == 31,
              "releasing action preserves move and look ownership");
        m.up(7, m.moveX, m.moveY);
        near(m.lx, 0, "releasing movement clears axis");
        m.move(31, 1134, 412);
        near(m.lookDx, 10, "camera survives movement finger release");
        near(m.lookDy, 5, "downward camera displacement keeps its sign");
        m.move(31, Float.NaN, Float.POSITIVE_INFINITY);
        near(m.lookDx, 10, "invalid touch coordinates cannot poison camera delta");
        check(m.down(31, 1700, 800) == TouchControlModel.LOOK, "duplicate down preserves existing ownership and origin");
        m.up(31, 1134, 412);
        check(m.lookPointer == -1, "camera ownership clears on lift");
        m.lookDx = m.lookDy = 0;
        m.move(31, 1500, 900);
        near(m.lookDx, 0, "lifted pointer cannot move camera");
        m.up(123, 1700, 700);
        check(m.down(402, 1100, 420) == TouchControlModel.LOOK, "new finger can acquire released camera");
        press(m, 8, TouchControlModel.X);
        m.clear();
        check(m.buttons == 0 && m.movePointer == -1 && m.lookPointer == -1, "cancel releases every pointer and button");
        near(m.lx, 0, "cancel clears horizontal axis"); near(m.ly, 0, "cancel clears vertical axis");
        near(m.lookDx, 0, "cancel clears pending camera delta"); near(m.lookDy, 0, "cancel clears pending vertical delta");
        m.move(402, 1400, 600);
        near(m.lookDx, 0, "canceled camera finger stays released");
    }
    private static void buttonsAndTransitions() {
        TouchControlModel m = model(TouchControlModel.BENDER, 0, true);
        press(m, 1, TouchControlModel.SPECIAL);
        check(m.isPressed(TouchControlModel.A) && m.isPressed(TouchControlModel.X), "special holds the original A+X combination");
        press(m, 17, TouchControlModel.A);
        release(m, 1, TouchControlModel.SPECIAL);
        check(m.isPressed(TouchControlModel.A) && !m.isPressed(TouchControlModel.X), "releasing combo preserves independently held A");
        m.setState(state(TouchControlModel.FRY, 2, 0, true));
        check(m.buttons == 0, "character change releases old held actions");
        press(m, 23, TouchControlModel.A);
        m.down(55, 1100, 420);
        m.setState(state(TouchControlModel.FRY, 3, 4, true));
        check(m.isPressed(TouchControlModel.A) && m.lookPointer == 55, "weapon/context updates preserve valid held pointers");
        m.move(55, 1110, 430);
        near(m.lookDx, 10, "context update preserves relative camera origin");
        press(m, 67, TouchControlModel.AIM);
        m.setState(state(TouchControlModel.FRY, 1, 0, true));
        check(!m.isPressed(TouchControlModel.AIM) && m.isPressed(TouchControlModel.A), "weapon hiding aim drops its held input without dropping action");
        m.setState(state(TouchControlModel.FRY, 1, 0, false));
        check(m.buttons == 0 && m.lookPointer == -1, "entering menu releases gameplay input");
        for (int character : new int[]{TouchControlModel.FRY, TouchControlModel.BENDER, TouchControlModel.LEELA, TouchControlModel.ZOIDBERG}) {
            m = model(character, 2, true);
            for (int b = 0; b < TouchControlModel.COUNT; ++b) if (m.visible[b]) {
                check(m.hit(m.cx[b], m.cy[b]) == b, "visible button " + b + " excludes camera for character " + character);
            }
        }
    }
    private static void visibility() {
        TouchControlModel m = model(TouchControlModel.FRY, 1, true);
        check(m.visible[TouchControlModel.A] && m.visible[TouchControlModel.X] && !m.visible[TouchControlModel.AIM], "Fry melee profile hides firearm aim");
        m.setState(state(TouchControlModel.FRY, 2, 0, true));
        check(m.visible[TouchControlModel.AIM] && m.visible[TouchControlModel.PREVIOUS] && m.visible[TouchControlModel.NEXT], "armed Fry exposes aim and weapon switching");
        for (int character : new int[]{TouchControlModel.BENDER, TouchControlModel.LEELA}) {
            m = model(character, 0, true);
            check(m.visible[TouchControlModel.SPECIAL] && !m.visible[TouchControlModel.PREVIOUS] && !m.visible[TouchControlModel.NEXT], "melee character has special without firearm switching");
        }
        m = model(TouchControlModel.LEELA, 0, true);
        check(m.visible[TouchControlModel.DODGE] && !m.visible[TouchControlModel.AIM], "Leela exposes dodge without overlapping aim");
        m = model(TouchControlModel.ZOIDBERG, 0, true);
        check(!m.cameraEnabled() && !m.visible[TouchControlModel.AIM] && !m.visible[TouchControlModel.SPECIAL]
              && !m.visible[TouchControlModel.PREVIOUS], "Zoidberg minigame removes camera and combat controls");
        check(m.visible[TouchControlModel.B] && m.visible[TouchControlModel.Y], "Zoidberg keeps minigame action and interact");
    }
    private static void menus() {
        TouchControlModel m = model(TouchControlModel.FRY, 2, false);
        check(!m.cameraEnabled() && m.hit(1100, 420) == TouchControlModel.NONE, "menu screen cannot capture camera drag");
        for (int b : new int[]{TouchControlModel.A, TouchControlModel.B, TouchControlModel.BACK}) {
            press(m, 25, b);
            check(m.isPressed(b), "menu accepts navigation button " + b);
            release(m, 25, b);
            check(m.buttons == 0, "menu button releases cleanly");
        }
        m.down(42, m.moveX, m.moveY);
        float[][] directions = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        int[] expected = {4, 8, 1, 2};
        for (int i = 0; i < directions.length; ++i) {
            m.move(42, m.moveX + directions[i][0] * m.moveRadius, m.moveY + directions[i][1] * m.moveRadius);
            check(m.buttons == expected[i], "menu joystick sends one D-pad direction " + i);
        }
        m.up(42, m.moveX, m.moveY);
        check(m.buttons == 0, "menu joystick has no stuck D-pad on release");
    }
    private static void hiddenControls() {
        TouchControlModel m = model(TouchControlModel.FRY, 2, true);
        press(m, 9, TouchControlModel.A);
        m.down(7, m.moveX, m.moveY);
        m.move(7, m.moveX + m.moveRadius, m.moveY);
        m.down(31, 1100, 420);
        m.move(31, 1110, 430);
        m.setTouchVisible(false);
        check(m.buttons == 0 && m.lx == 0 && m.ly == 0 && m.lookDx == 0 && m.lookDy == 0,
              "hiding HUD releases held gameplay and pending camera input");
        check(m.movePointer == -1 && m.lookPointer == -1, "hidden HUD drops old gesture owners");
        check(m.hit(m.moveX, m.moveY) == TouchControlModel.NONE && m.hit(1100, 420) == TouchControlModel.NONE,
              "hidden movement and camera zones cannot capture fingers");
        for (int b = 0; b < TouchControlModel.COUNT; ++b) {
            check(m.visible[b] == (b == TouchControlModel.PAUSE || b == TouchControlModel.SETTINGS),
                  "only toolbar remains visible with hidden HUD, button " + b);
        }
        check(press(m, 21, TouchControlModel.SETTINGS) == TouchControlModel.SETTINGS,
              "settings remains reachable to restore hidden HUD");
        release(m, 21, TouchControlModel.SETTINGS);
        m.setTouchVisible(true);
        check(m.visible[TouchControlModel.A] && m.cameraEnabled(), "showing HUD restores current profile");
        m.move(31, 1200, 500);
        near(m.lookDx, 0, "restoring HUD cannot revive stale camera finger");
    }
    private static void layout() {
        TouchControlModel m = model(TouchControlModel.FRY, 2, true);
        for (float[] size : new float[][]{{1280, 720}, {1920, 1080}, {2400, 1080}, {2560, 1600}}) {
            float w = size[0], h = size[1], l = 60, t = 24, r = 36, b = 32;
            m.layout(w, h, 2, l, t, r, b);
            for (int button = 0; button < TouchControlModel.COUNT; ++button) if (m.visible[button]) {
                float radius = m.radius[button];
                check(radius > 0 && Float.isFinite(radius), "button radius remains finite across landscape sizes");
                check(m.cx[button] - radius >= l - .01f && m.cx[button] + radius <= w - r + .01f
                      && m.cy[button] - radius >= t - .01f && m.cy[button] + radius <= h - b + .01f,
                      "visible control stays inside safe insets");
            }
            check(m.moveX - m.moveRadius >= l && m.moveY + m.moveRadius <= h - b, "joystick stays within safe area");
            press(m, 9, TouchControlModel.A);
            m.down(37, m.moveX, m.moveY);
            m.move(37, m.moveX + m.moveRadius, m.moveY);
            m.down(81, l + (w - l - r) * .55f, t + (h - t - b) * .4f);
            m.layout(w - 20, h - 10, 2, l, t, r, b);
            check(m.buttons == 0 && m.movePointer == -1 && m.lookPointer == -1 && m.lx == 0 && m.ly == 0,
                  "resize releases gestures anchored to old coordinates");
        }
    }
    public static void main(String[] args) {
        gestures(); buttonsAndTransitions(); visibility(); menus(); hiddenControls(); layout();
        System.out.println("Touch control model tests passed (" + checks + " checks)");
    }
}
