package jp.co.taito.groovecoasterzero;

import android.app.Activity;
import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.InsetDrawable;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.util.DisplayMetrics;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * Song search box over the song list. It only shows while the list is on screen
 * (native side reports when MenuRouteView is being drawn); typing filters the
 * list in libgcp.so (search.cpp). The query stays until cleared, so returning to
 * the list keeps the filter.
 *
 * This overlay draws only the frame and takes the touches and keyboard input; the
 * text inside (query, placeholder, match count, clear "X") is drawn by the game
 * in its own pixel font. Queries the font can't show (non-ASCII) are drawn here.
 */
public final class GC2PlusSearch {
    private static final int CYAN = 0xFF38D8FF;
    private static final int FRAME = 0xFFDCDCDC;
    // Hide as soon as the list stops being drawn (the list draws every frame).
    private static final int POLL_MS = 40;
    private static final int HIDE_AFTER_MS = 100;

    // The touch area extends this many box heights above and below the frame.
    private static final float TOUCH_PAD = 0.4f;

    // Status buffer layout (see search.cpp).
    private static final int UI_FLAGS = 12, BOX = 16, TEXT_LEN = 32, TEXT = 36, TEXT_MAX = 123;
    private static final int UI_ATTACHED = 1, UI_DRAW_TEXT = 2, UI_FOCUSED = 4;

    static {
        try {
            System.loadLibrary("gcp");
        } catch (UnsatisfiedLinkError ignored) {
        }
    }

    private static native void nativeSetQuery(String query);
    /**
     * Status shared with the GL thread, read and written here without JNI calls
     * (frequent JNI calls from this thread crashed MuMu's ARM translator):
     * [0] int64 uptime ms when the song list was last drawn, [8] int32 matches,
     * then the box state this class writes.
     */
    private static native java.nio.ByteBuffer nativeStatus();

    private static java.nio.ByteBuffer status;

    private static LinearLayout bar;
    private static EditText field;
    private static View clear;
    private static GradientDrawable frame;
    private static int stroke;
    // Last applied box in pixels (the game lays it out; see applyLayout).
    private static int boxLeft, boxTop, boxRight, boxBottom;
    private static final Handler handler = new Handler(Looper.getMainLooper());

    private GC2PlusSearch() {}

    /** Called from MainActivity.onCreate. */
    public static void attach(final Activity activity) {
        try {
            build(activity);
        } catch (Throwable t) {
            android.util.Log.e("GC2PlusSearch", "attach failed", t);
        }
    }

    private static void build(final Activity activity) {
        status = nativeStatus().order(java.nio.ByteOrder.nativeOrder());
        activity.getWindow().setSoftInputMode(
                WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING
                        | WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN);
        DisplayMetrics dm = activity.getResources().getDisplayMetrics();
        stroke = Math.max(1, Math.round(Math.min(dm.widthPixels, dm.heightPixels) / 540f));
        frame = new GradientDrawable();
        frame.setColor(Color.TRANSPARENT);
        frame.setStroke(stroke, FRAME);

        bar = new LinearLayout(activity);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        // Holds focus itself so the field isn't focused (and the caret shown)
        // until it is tapped.
        bar.setFocusable(true);
        bar.setFocusableInTouchMode(true);
        bar.setDescendantFocusability(android.view.ViewGroup.FOCUS_BEFORE_DESCENDANTS);

        field = new EditText(activity) {
            // The caret is drawn at the end of the text by the game, so keep it there.
            @Override protected void onSelectionChanged(int start, int end) {
                int n = length();
                if (start != n || end != n) {
                    setSelection(n);
                    return;
                }
                super.onSelectionChanged(start, end);
            }
        };
        field.setTextColor(Color.TRANSPARENT);
        field.setCursorVisible(false);
        field.setSingleLine(true);
        field.setBackground(null);
        field.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        field.setImeOptions(EditorInfo.IME_ACTION_SEARCH | EditorInfo.IME_FLAG_NO_EXTRACT_UI);
        bar.addView(field, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f));

        // Clear button: the "X" in the square at the right end of the box.
        clear = new View(activity);
        clear.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                field.setText("");
                hideKeyboard(activity);
            }
        });
        bar.addView(clear, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT));

        field.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int a, int b, int c) {}
            @Override public void onTextChanged(CharSequence s, int a, int b, int c) {}
            @Override public void afterTextChanged(Editable s) {
                nativeSetQuery(s.toString().trim());
                writeState();
            }
        });
        field.setOnFocusChangeListener(new View.OnFocusChangeListener() {
            @Override public void onFocusChange(View v, boolean hasFocus) {
                frame.setStroke(stroke, hasFocus ? CYAN : FRAME);
                writeState();
            }
        });
        field.setOnEditorActionListener(new TextView.OnEditorActionListener() {
            @Override public boolean onEditorAction(TextView v, int actionId, KeyEvent event) {
                hideKeyboard(activity);
                return true;
            }
        });

        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(0, 0);
        lp.gravity = Gravity.TOP | Gravity.START;
        bar.setVisibility(View.GONE);
        activity.addContentView(bar, lp);
        writeState();

        handler.post(new Runnable() {
            @Override public void run() {
                poll(activity);
                handler.postDelayed(this, POLL_MS);
            }
        });
    }

    /** Publishes the query text and flags for the game to draw. */
    private static void writeState() {
        String text = field.getText().toString();
        boolean ascii = true;
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            if (c < 0x20 || c > 0x7e) {
                ascii = false;
                break;
            }
        }
        // The pixel font can't draw it: show the text in the Android font instead.
        field.setTextColor(ascii ? Color.TRANSPARENT : Color.WHITE);
        String upper = ascii ? text.toUpperCase(java.util.Locale.ROOT) : "";
        int n = Math.min(upper.length(), TEXT_MAX);
        status.putInt(TEXT_LEN, 0);
        for (int i = 0; i < n; i++) status.put(TEXT + i, (byte) upper.charAt(i));
        status.put(TEXT + n, (byte) 0);
        status.putInt(TEXT_LEN, ascii ? n : text.length());
        int flags = UI_ATTACHED | (ascii ? UI_DRAW_TEXT : 0) | (field.hasFocus() ? UI_FOCUSED : 0);
        status.putInt(UI_FLAGS, flags);
    }

    private static View surface;

    /** The game's GL surface; the box rect is relative to it. */
    private static View findSurface(View v) {
        if (v instanceof android.view.SurfaceView) return v;
        if (v instanceof android.view.ViewGroup) {
            android.view.ViewGroup g = (android.view.ViewGroup) v;
            for (int i = 0; i < g.getChildCount(); i++) {
                View found = findSurface(g.getChildAt(i));
                if (found != null) return found;
            }
        }
        return null;
    }

    /**
     * Places the frame on the box the game laid out (from the sort buttons,
     * published as fractions of the GL surface). Returns false
     * while there is no box yet.
     */
    private static boolean applyLayout() {
        float fl = status.getFloat(BOX), ft = status.getFloat(BOX + 4);
        float fr = status.getFloat(BOX + 8), fb = status.getFloat(BOX + 12);
        View parent = (View) bar.getParent();
        if (fr <= fl || fb <= ft || parent == null) return false;
        if (surface == null) surface = findSurface(parent.getRootView());
        View ref = surface != null ? surface : parent;
        if (ref.getWidth() == 0) return false;
        int[] refPos = new int[2], parentPos = new int[2];
        ref.getLocationOnScreen(refPos);
        parent.getLocationOnScreen(parentPos);
        int ox = refPos[0] - parentPos[0], oy = refPos[1] - parentPos[1];
        int l = ox + Math.round(fl * ref.getWidth()), r = ox + Math.round(fr * ref.getWidth());
        int t = oy + Math.round(ft * ref.getHeight()), b = oy + Math.round(fb * ref.getHeight());
        if (l == boxLeft && t == boxTop && r == boxRight && b == boxBottom) return true;
        boxLeft = l;
        boxTop = t;
        boxRight = r;
        boxBottom = b;

        // The view extends above and below the frame for a bigger touch target.
        int h = b - t, pad = Math.round(h * TOUCH_PAD);
        bar.setBackground(new InsetDrawable(frame, 0, pad, 0, pad));
        field.setTextSize(TypedValue.COMPLEX_UNIT_PX, h * 0.5f);
        field.setPadding(Math.round(h * 0.3f), 0, 0, 0);
        clear.getLayoutParams().width = h;
        FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams) bar.getLayoutParams();
        lp.width = r - l;
        lp.height = h + 2 * pad;
        lp.leftMargin = l;
        lp.topMargin = t - pad;
        bar.setLayoutParams(lp);
        return true;
    }

    private static void poll(Activity activity) {
        long lastDraw = status.getLong(0);
        boolean visible = lastDraw != 0 && android.os.SystemClock.uptimeMillis() - lastDraw < HIDE_AFTER_MS
                && applyLayout();
        if (visible && bar.getVisibility() != View.VISIBLE) {
            bar.setVisibility(View.VISIBLE);
            bar.bringToFront();
            bar.requestFocus();
        } else if (!visible && bar.getVisibility() == View.VISIBLE) {
            hideKeyboard(activity);
            bar.setVisibility(View.GONE);
        }
    }

    private static void hideKeyboard(Activity activity) {
        InputMethodManager imm = (InputMethodManager) activity.getSystemService(Context.INPUT_METHOD_SERVICE);
        if (imm != null) imm.hideSoftInputFromWindow(field.getWindowToken(), 0);
        bar.requestFocus();
    }
}
