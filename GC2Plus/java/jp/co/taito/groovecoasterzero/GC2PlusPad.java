package jp.co.taito.groovecoasterzero;

import android.app.Activity;
import android.util.DisplayMetrics;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;

/**
 * Gamepad play, modelled on Groove Coaster Wai Wai Party's standard controls.
 * Each side of the pad is a "booster" driving one virtual finger, fed to the
 * engine through JNILib.onTouchEvents (same int[] layout JNIActivity.onTouchEvent
 * builds: [count, (action, id, x, y) per pointer], action 0=down 1=up 2=move).
 *
 *   left booster:  D-pad (keys or hat axes), left stick, L1/L2
 *   right booster: face buttons by position (top/right/bottom/left), right stick, R1/R2
 *
 * Any button press on a side is a HIT (hold it for HOLD, mash for BEAT). Directional
 * and face buttons also push the finger toward their direction, so the matching
 * button performs a SLIDE. Tilting a stick drags the finger (SLIDE / SLIDE HOLD,
 * waggle for SCRATCH). Both sides together give CRITICAL / DUAL HOLD / DUAL SLIDE.
 * Only active while a stage is on screen, so menus keep their normal behaviour.
 */
public final class GC2PlusPad {
    private static final int UP = 0, RIGHT = 1, DOWN = 2, LEFT = 3, NONE = -1;
    private static final int ACTION_DOWN = 0, ACTION_UP = 1, ACTION_MOVE = 2;
    // The engine only registers hits from the low touch slots (ids 6-9 were ignored
    // in testing), so the boosters use ids 0-3. Touching the screen while a pad
    // finger is down can clash with them; pad play doesn't need the screen.
    private static final int POINTER_ID_BASE = 0;
    private static final float STICK_PRESS = 0.5f;   // tilt that puts the finger down
    private static final float STICK_RELEASE = 0.25f; // tilt below which it may lift
    private static final long STICK_RELEASE_MS = 40;  // must stay centred this long
    // The engine samples touches once per 60Hz tick; a press shorter than a tick
    // would be invisible, so every virtual press lasts at least this long.
    private static final long MIN_PRESS_MS = 40;

    private static final android.os.Handler handler = new android.os.Handler(android.os.Looper.getMainLooper());

    private static boolean libLoaded;

    static {
        try {
            System.loadLibrary("gcp");
            libLoaded = true;
        } catch (UnsatisfiedLinkError e) {
            libLoaded = false;
        }
    }

    /** True while a stage is being drawn (implemented in libgcp.so). */
    private static native boolean nativeInGame();

    private static final class Booster {
        final int baseId;
        int id;             // alternates per press so re-taps are always new touches
        final float homeX;  // fraction of the screen width
        long downAt;
        boolean releasePending;
        final boolean[] dirs = new boolean[4];  // directional buttons held
        int plainHeld;      // shoulder/trigger buttons held
        float stickX, stickY;
        boolean stickActive;
        long stickCentredSince = -1;
        boolean down;
        int x, y;

        Booster(int baseId, float homeX) {
            this.baseId = baseId;
            this.id = baseId;
            this.homeX = homeX;
        }

        boolean anyButton() {
            return plainHeld > 0 || dirs[0] || dirs[1] || dirs[2] || dirs[3];
        }
    }

    // Ids base and base+2 alternate for each booster.
    private static final Booster left = new Booster(POINTER_ID_BASE, 0.3f);
    private static final Booster right = new Booster(POINTER_ID_BASE + 1, 0.7f);

    private GC2PlusPad() {}

    private static boolean fromGamepad(int source) {
        return (source & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (source & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
                || (source & InputDevice.SOURCE_DPAD) == InputDevice.SOURCE_DPAD;
    }

    /** From MainActivity.dispatchKeyEvent; true = consumed. */
    public static boolean handleKey(Activity activity, KeyEvent event) {
        Booster b;
        int dir;
        switch (event.getKeyCode()) {
            case KeyEvent.KEYCODE_DPAD_UP: b = left; dir = UP; break;
            case KeyEvent.KEYCODE_DPAD_RIGHT: b = left; dir = RIGHT; break;
            case KeyEvent.KEYCODE_DPAD_DOWN: b = left; dir = DOWN; break;
            case KeyEvent.KEYCODE_DPAD_LEFT: b = left; dir = LEFT; break;
            case KeyEvent.KEYCODE_BUTTON_L1: case KeyEvent.KEYCODE_BUTTON_L2: b = left; dir = NONE; break;
            // Face buttons by position (Android uses the Xbox layout: Y top, B right, A bottom, X left).
            case KeyEvent.KEYCODE_BUTTON_Y: b = right; dir = UP; break;
            case KeyEvent.KEYCODE_BUTTON_B: b = right; dir = RIGHT; break;
            case KeyEvent.KEYCODE_BUTTON_A: b = right; dir = DOWN; break;
            case KeyEvent.KEYCODE_BUTTON_X: b = right; dir = LEFT; break;
            case KeyEvent.KEYCODE_BUTTON_R1: case KeyEvent.KEYCODE_BUTTON_R2: b = right; dir = NONE; break;
            default: return false;
        }
        if (!fromGamepad(event.getSource())) return false;
        if (!inGame()) {
            reset();
            return false;  // let menus handle the pad as usual (B = back, etc.)
        }
        int action = event.getAction();
        if (action != KeyEvent.ACTION_DOWN && action != KeyEvent.ACTION_UP) return true;
        boolean pressed = action == KeyEvent.ACTION_DOWN;
        if (pressed && event.getRepeatCount() > 0) return true;
        setButton(activity, b, dir, pressed);
        return true;
    }

    /** From MainActivity.dispatchGenericMotionEvent; true = consumed. */
    public static boolean handleMotion(Activity activity, MotionEvent event) {
        if (!fromGamepad(event.getSource()) || event.getAction() != MotionEvent.ACTION_MOVE) return false;
        if (!inGame()) {
            reset();
            return false;
        }
        // D-pads that report as a hat switch.
        float hx = event.getAxisValue(MotionEvent.AXIS_HAT_X);
        float hy = event.getAxisValue(MotionEvent.AXIS_HAT_Y);
        setHat(activity, UP, hy < -0.5f);
        setHat(activity, DOWN, hy > 0.5f);
        setHat(activity, LEFT, hx < -0.5f);
        setHat(activity, RIGHT, hx > 0.5f);

        setStick(activity, left, event.getAxisValue(MotionEvent.AXIS_X), event.getAxisValue(MotionEvent.AXIS_Y));
        setStick(activity, right, event.getAxisValue(MotionEvent.AXIS_Z), event.getAxisValue(MotionEvent.AXIS_RZ));
        return true;
    }

    private static boolean inGame() {
        return libLoaded && nativeInGame();
    }

    private static void setHat(Activity activity, int dir, boolean pressed) {
        if (left.dirs[dir] != pressed) setButton(activity, left, dir, pressed);
    }

    private static void setButton(Activity activity, Booster b, int dir, boolean pressed) {
        boolean wasAny = b.anyButton();
        if (dir == NONE) b.plainHeld = Math.max(0, b.plainHeld + (pressed ? 1 : -1));
        else b.dirs[dir] = pressed;
        // Every new press is a fresh hit, even if the finger is already down
        // (another button held, the stick tilted, or a short press still being
        // held for MIN_PRESS_MS): lift it and land a new finger.
        if (pressed && b.down && (wasAny || b.stickActive || b.releasePending)) {
            send(b, ACTION_UP);
            b.id = b.id == b.baseId ? b.baseId + 2 : b.baseId;
        }
        update(activity, b);
    }

    private static void setStick(final Activity activity, final Booster b, float sx, float sy) {
        float mag = (float) Math.sqrt(sx * sx + sy * sy);
        b.stickX = sx;
        b.stickY = sy;
        if (mag >= STICK_PRESS) {
            b.stickActive = true;
            b.stickCentredSince = -1;
        } else if (b.stickActive && mag < STICK_RELEASE && b.stickCentredSince < 0) {
            // Debounce: a quick waggle through the centre keeps the finger down.
            // Sticks only report changes, so the release has to come from a timer.
            final long since = android.os.SystemClock.uptimeMillis();
            b.stickCentredSince = since;
            handler.postDelayed(new Runnable() {
                @Override public void run() {
                    if (b.stickActive && b.stickCentredSince == since) {
                        b.stickActive = false;
                        b.stickCentredSince = -1;
                        update(activity, b);
                    }
                }
            }, STICK_RELEASE_MS);
        } else if (mag >= STICK_RELEASE) {
            b.stickCentredSince = -1;
        }
        update(activity, b);
    }

    private static void reset() {
        for (Booster b : new Booster[] {left, right}) {
            if (b.down) send(b, ACTION_UP);
            b.plainHeld = 0;
            for (int i = 0; i < 4; i++) b.dirs[i] = false;
            b.stickActive = false;
            b.stickCentredSince = -1;
        }
    }

    private static void update(Activity activity, Booster b) {
        DisplayMetrics dm = activity.getResources().getDisplayMetrics();
        int w = dm.widthPixels, h = dm.heightPixels;
        float reach = Math.min(w, h) * 0.12f;
        int homeX = (int) (w * b.homeX), homeY = (int) (h * 0.6f);

        // Buttons give an 8-way direction; otherwise the stick gives an analog one.
        float dx = (b.dirs[RIGHT] ? 1 : 0) - (b.dirs[LEFT] ? 1 : 0);
        float dy = (b.dirs[DOWN] ? 1 : 0) - (b.dirs[UP] ? 1 : 0);
        if (dx == 0 && dy == 0 && b.stickActive) {
            dx = b.stickX;
            dy = b.stickY;
        }
        float len = (float) Math.sqrt(dx * dx + dy * dy);
        int x = homeX, y = homeY;
        if (len > 0.01f) {
            float scale = Math.min(len, 1f) / len;
            x += (int) (dx * scale * reach);
            y += (int) (dy * scale * reach);
        }
        boolean active = b.anyButton() || b.stickActive;

        if (active && !b.down) {
            // Land at home first so a direction registers as a slide.
            b.x = homeX;
            b.y = homeY;
            send(b, ACTION_DOWN);
            if (x != homeX || y != homeY) {
                b.x = x;
                b.y = y;
                send(b, ACTION_MOVE);
            }
        } else if (active) {
            if (x != b.x || y != b.y) {
                b.x = x;
                b.y = y;
                send(b, ACTION_MOVE);
            }
        } else if (b.down) {
            long held = android.os.SystemClock.uptimeMillis() - b.downAt;
            if (held < MIN_PRESS_MS) {
                if (!b.releasePending) {
                    b.releasePending = true;
                    final Activity a = activity;
                    final Booster fb = b;
                    handler.postDelayed(new Runnable() {
                        @Override public void run() {
                            fb.releasePending = false;
                            update(a, fb);
                        }
                    }, MIN_PRESS_MS - held);
                }
                return;
            }
            send(b, ACTION_UP);
        }
    }

    // Like Android's own MotionEvents, every packet lists all fingers that are down:
    // the engine drops a touch slot that receives no event in a frame.
    private static void send(Booster b, int action) {
        if (action == ACTION_DOWN) b.downAt = android.os.SystemClock.uptimeMillis();
        if (action == ACTION_UP) b.releasePending = false;
        Booster other = b == left ? right : left;
        boolean withOther = other.down;
        int[] ev = new int[withOther ? 9 : 5];
        ev[0] = withOther ? 2 : 1;
        put(ev, 1, action, b);
        if (withOther) put(ev, 5, ACTION_MOVE, other);
        b.down = action != ACTION_UP;
        JNILib.onTouchEvents(ev);
        if (b.down) scheduleKeepAlive();
    }

    private static void put(int[] ev, int at, int action, Booster b) {
        ev[at] = action;
        ev[at + 1] = b.id;
        ev[at + 2] = b.x;
        ev[at + 3] = b.y;
    }

    private static boolean keepAliveScheduled;

    private static final Runnable keepAlive = new Runnable() {
        @Override public void run() {
            keepAliveScheduled = false;
            if (!left.down && !right.down) return;
            if (!inGame()) {
                reset();
                return;
            }
            Booster first = left.down ? left : right;
            Booster second = left.down && right.down ? right : null;
            int[] ev = new int[second != null ? 9 : 5];
            ev[0] = second != null ? 2 : 1;
            put(ev, 1, ACTION_MOVE, first);
            if (second != null) put(ev, 5, ACTION_MOVE, second);
            JNILib.onTouchEvents(ev);
            scheduleKeepAlive();
        }
    };

    private static void scheduleKeepAlive() {
        if (keepAliveScheduled) return;
        keepAliveScheduled = true;
        handler.postDelayed(keepAlive, 16);
    }
}
