// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

package maul.ui.tests;

import android.app.Activity;
import android.content.pm.PackageManager;
import android.graphics.Rect;
import android.os.Bundle;
import android.view.View;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityNodeInfo.AccessibilityAction;
import android.view.accessibility.AccessibilityNodeProvider;
import java.io.IOException;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/**
 * The Android accessibility adapter against Android's classes (record
 * mui-0008), in the emulator (tools/run_android_app.sh): the test's
 * native library (test/test_android_access.c) makes the adapter for a
 * view and a tree; the checks here ask the provider as clients do:
 * - the host's one child, the root; class names, texts, content
 *   descriptions, hints, states, ranges, live settings, headings;
 * - bounds on the screen at the scale;
 * - the actions offered and asked of the host; the screen reader's
 *   cursor; the input focus; the node under a place;
 * - the events an update makes, recorded in place of the provider's:
 *   a content description, a text or a state changed, or a change
 *   unsaid; the subtree; the view focused; none for no change; a text
 *   typed into, its change and its selection in UTF-16;
 * - text: granularities and the selection in the info, the caret moved
 *   by granularity through the host and extended, a label's words
 *   through a cursor of the provider's, the selection and the text set;
 * - a node gone, and the adapter gone, answering nothing.
 * It writes its failures and a closing "result: N failures" to
 * files/out.
 */
public final class TestActivity extends Activity {
    // Maul UI's actions (maul-ui/access.h).
    private static final int CLICK = 0;
    private static final int FOCUS = 1;
    private static final int INCREMENT = 5;
    private static final int DECREMENT = 6;
    private static final int SET_VALUE = 7;
    private static final int SET_SELECTION = 14;
    private static final int REPLACE_TEXT = 15;

    private PrintWriter out;
    private int failures;

    private static native long make(View host);

    private static native Object providerOf(long adapter);

    private static native int virtualOf(long adapter, long node);

    private static native int askedAction();

    private static native long askedTarget();

    private static native float askedValue();

    private static native int askedAnchor();

    private static native int askedFocus();

    private static native byte[] askedText();

    private static native String toldAfter(long adapter, int step);

    private static native void removeButton(long adapter);

    private static native void destroy(long adapter);

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        View host = new View(this);
        setContentView(host);
        host.post(() -> run(host));
    }

    private void check(boolean ok, String what) {
        if (!ok) {
            failures++;
            out.println("FAIL: " + what);
        }
    }

    private void run(View host) {
        try {
            out = new PrintWriter(openFileOutput("out", MODE_PRIVATE), true);
            String library = getPackageManager()
                    .getActivityInfo(getComponentName(), PackageManager.GET_META_DATA)
                    .metaData.getString("maul.ui.library");
            System.loadLibrary(library);
            test(host);
        } catch (IOException | PackageManager.NameNotFoundException e) {
            failures++;
            if (out != null) {
                out.println("FAIL: " + e);
            }
        }
        if (out != null) {
            out.println("result: " + failures + " failures");
            out.close();
        }
    }

    private static boolean offers(AccessibilityNodeInfo info, AccessibilityAction action) {
        return info.getActionList().contains(action);
    }

    private static boolean same(CharSequence a, String b) {
        return a == null ? b == null : a.toString().equals(b);
    }

    private void test(View host) {
        long adapter = make(host);
        check(adapter != 0, "made");
        if (adapter == 0) {
            return;
        }
        AccessibilityNodeProvider provider = (AccessibilityNodeProvider) providerOf(adapter);
        int root = virtualOf(adapter, 1);
        int button = virtualOf(adapter, 2);
        int label = virtualOf(adapter, 4);
        int field = virtualOf(adapter, 5);
        int box = virtualOf(adapter, 7);
        int slider = virtualOf(adapter, 8);
        int heading = virtualOf(adapter, 9);
        check(provider.createAccessibilityNodeInfo(View.NO_ID).getChildCount() == 1,
                "the host's one child");
        testNodes(provider, host, root, button, label, field, box, slider, heading);
        testActions(provider, button, label, field, box, slider);
        testText(provider, label, field, button);
        testEvents(adapter, root, button, label, field, box, slider);
        removeButton(adapter);
        check(provider.createAccessibilityNodeInfo(button) == null
                && provider.createAccessibilityNodeInfo(root).getChildCount() == 5,
                "a node gone answers nothing");
        destroy(adapter);
        check(provider.createAccessibilityNodeInfo(View.NO_ID).getChildCount() == 0
                && provider.createAccessibilityNodeInfo(root) == null
                && !provider.performAction(field, AccessibilityNodeInfo.ACTION_CLICK, null),
                "the adapter gone answers nothing");
    }

    // isChecked() is the one from Android 11 on; newer platforms deprecate
    // it for a form Android 11 lacks.
    @SuppressWarnings("deprecation")
    private void testNodes(AccessibilityNodeProvider provider, View host, int root, int button,
            int label, int field, int box, int slider, int heading) {
        AccessibilityNodeInfo info = provider.createAccessibilityNodeInfo(root);
        check(same(info.getClassName(), "android.view.View")
                && same(info.getContentDescription(), "Main") && info.getChildCount() == 6,
                "the root: its name, its children with the generic flattened");
        info = provider.createAccessibilityNodeInfo(button);
        check(same(info.getClassName(), "android.widget.Button")
                && same(info.getContentDescription(), "OK") && info.getText() == null
                && info.isFocusable() && info.isFocused() && info.isEnabled()
                && info.isVisibleToUser(), "a button");
        int[] at = new int[2];
        host.getLocationOnScreen(at);
        Rect bounds = new Rect();
        info.getBoundsInScreen(bounds);
        check(bounds.equals(new Rect(at[0] + 20, at[1] + 20, at[0] + 220, at[1] + 100)),
                "bounds on the screen at the scale: " + bounds);
        info = provider.createAccessibilityNodeInfo(label);
        check(same(info.getClassName(), "android.widget.TextView")
                && same(info.getText(), "Hi 😀") && info.getContentDescription() == null,
                "a label, its name its text, past the Basic Multilingual Plane");
        info = provider.createAccessibilityNodeInfo(field);
        check(same(info.getClassName(), "android.widget.EditText") && same(info.getText(), "Ada")
                && same(info.getHintText(), "Name") && info.isEditable()
                && info.getContentDescription() == null,
                "a text field: its value its text, its name its hint");
        info = provider.createAccessibilityNodeInfo(box);
        check(same(info.getClassName(), "android.widget.CheckBox") && info.isCheckable()
                && info.isChecked(), "a checked check box");
        info = provider.createAccessibilityNodeInfo(slider);
        AccessibilityNodeInfo.RangeInfo range = info.getRangeInfo();
        check(same(info.getClassName(), "android.widget.SeekBar") && range != null
                && range.getType() == AccessibilityNodeInfo.RangeInfo.RANGE_TYPE_FLOAT
                && range.getCurrent() == 30.0f && range.getMax() == 100.0f
                && same(info.getHintText(), "Louder")
                && same(info.getStateDescription(), "30 percent"),
                "a range, its description its hint, its value text its state");
        info = provider.createAccessibilityNodeInfo(heading);
        check(info.isHeading() && info.getLiveRegion() == View.ACCESSIBILITY_LIVE_REGION_POLITE,
                "a live heading");
    }

    private void told(long adapter, int step, String expected, String what) {
        String got = toldAfter(adapter, step);
        check(got.equals(expected), what + ": told \"" + got + "\"");
    }

    // Content changes (2048) with their types, a focus (8).
    private void testEvents(long adapter, int root, int button, int label, int field, int box,
            int slider) {
        told(adapter, 1, "2048 " + button + " 4;", "a name as content description");
        told(adapter, 2, "2048 " + label + " 2;", "a label's name as text");
        told(adapter, 3, "2048 " + slider + " 64;", "a value text as state");
        told(adapter, 4, "2048 " + box + " 0;", "a state, unsaid");
        told(adapter, 5, "2048 " + field + " 0;", "a text field's name, its hint, unsaid");
        told(adapter, 6, "", "no change");
        told(adapter, 7, "2048 " + root + " 1;8 " + field + " 0;",
                "the focus moved: the subtree, then the view focused");
        String got = toldAfter(adapter, 8);
        check(got.contains("8 " + box + " 0;"),
                "a focused window's active descendant: the view focused, told \"" + got + "\"");
        told(adapter, 9, "2048 " + field + " 2;16 " + field + " 3 0 1 Ada;8192 " + field
                + " 4 4 4 -;", "a field typed into: its text and its selection");
    }

    private boolean asked(int action, long target) {
        return askedAction() == action && askedTarget() == target;
    }

    private static Bundle moving(int granularity, boolean extend) {
        Bundle arguments = new Bundle();
        arguments.putInt(AccessibilityNodeInfo.ACTION_ARGUMENT_MOVEMENT_GRANULARITY_INT,
                granularity);
        arguments.putBoolean(AccessibilityNodeInfo.ACTION_ARGUMENT_EXTEND_SELECTION_BOOLEAN,
                extend);
        return arguments;
    }

    private boolean selectionAsked(int anchor, int focus) {
        return asked(SET_SELECTION, 5) && askedAnchor() == anchor && askedFocus() == focus;
    }

    // The field "Ada" (UTF-16 as its bytes) with the caret at 3; the
    // label "Hi 😀", 5 units of 7 bytes, of the words "Hi" and "😀".
    private void testText(AccessibilityNodeProvider provider, int label, int field, int button) {
        final int all = AccessibilityNodeInfo.MOVEMENT_GRANULARITY_CHARACTER
                | AccessibilityNodeInfo.MOVEMENT_GRANULARITY_WORD
                | AccessibilityNodeInfo.MOVEMENT_GRANULARITY_LINE
                | AccessibilityNodeInfo.MOVEMENT_GRANULARITY_PARAGRAPH;
        final int word = AccessibilityNodeInfo.MOVEMENT_GRANULARITY_WORD;
        final int character = AccessibilityNodeInfo.MOVEMENT_GRANULARITY_CHARACTER;
        final int next = AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY;
        final int previous = AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY;
        AccessibilityNodeInfo info = provider.createAccessibilityNodeInfo(field);
        check(info.getMovementGranularities() == all && info.getTextSelectionStart() == 3
                && info.getTextSelectionEnd() == 3
                && offers(info, AccessibilityAction.ACTION_NEXT_AT_MOVEMENT_GRANULARITY)
                && offers(info, AccessibilityAction.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY)
                && offers(info, AccessibilityAction.ACTION_SET_SELECTION)
                && offers(info, AccessibilityAction.ACTION_SET_TEXT),
                "a field being edited: its granularities, its caret, its text actions");
        info = provider.createAccessibilityNodeInfo(label);
        check(info.getMovementGranularities() == all && info.getTextSelectionStart() == -1
                && !offers(info, AccessibilityAction.ACTION_SET_SELECTION),
                "a label's granularities, no selection");
        check(provider.createAccessibilityNodeInfo(button).getMovementGranularities() == 0,
                "a button's none");
        check(provider.performAction(field, previous, moving(word, false))
                && selectionAsked(0, 0), "the caret moved back a word through the host");
        check(provider.performAction(field, previous, moving(character, true))
                && selectionAsked(3, 2), "the selection extended back a character");
        check(!provider.performAction(field, next, moving(character, false)),
                "no character past the end");
        Bundle range = new Bundle();
        range.putInt(AccessibilityNodeInfo.ACTION_ARGUMENT_SELECTION_START_INT, 1);
        range.putInt(AccessibilityNodeInfo.ACTION_ARGUMENT_SELECTION_END_INT, 2);
        check(provider.performAction(field, AccessibilityNodeInfo.ACTION_SET_SELECTION, range)
                && selectionAsked(1, 2), "a selection set");
        range.putInt(AccessibilityNodeInfo.ACTION_ARGUMENT_SELECTION_END_INT, 9);
        check(!provider.performAction(field, AccessibilityNodeInfo.ACTION_SET_SELECTION, range),
                "no selection past the text");
        Bundle text = new Bundle();
        text.putCharSequence(AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE,
                "Zo\u00eb \ud83d\ude00");
        check(provider.performAction(field, AccessibilityNodeInfo.ACTION_SET_TEXT, text)
                && asked(REPLACE_TEXT, 5) && askedAnchor() == 0 && askedFocus() == 3
                && Arrays.equals(askedText(),
                        "Zo\u00eb \ud83d\ude00".getBytes(StandardCharsets.UTF_8)),
                "the text set, past the Basic Multilingual Plane");
        check(provider.performAction(label, AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS, null)
                && provider.performAction(label, next, moving(word, false))
                && provider.performAction(label, next, moving(word, false))
                && !provider.performAction(label, next, moving(word, false))
                && provider.performAction(label, previous, moving(word, false))
                && provider.performAction(label,
                        AccessibilityNodeInfo.ACTION_CLEAR_ACCESSIBILITY_FOCUS, null),
                "a label's words through the provider's cursor, none past the last");
    }

    private void testActions(AccessibilityNodeProvider provider, int button, int label,
            int field, int box, int slider) {
        AccessibilityNodeInfo info = provider.createAccessibilityNodeInfo(button);
        check(offers(info, AccessibilityAction.ACTION_CLICK)
                && offers(info, AccessibilityAction.ACTION_CLEAR_FOCUS)
                && !offers(info, AccessibilityAction.ACTION_FOCUS)
                && offers(info, AccessibilityAction.ACTION_ACCESSIBILITY_FOCUS),
                "a focused button's actions");
        check(provider.performAction(button, AccessibilityNodeInfo.ACTION_CLICK, null)
                && asked(CLICK, 2), "a click");
        check(!provider.performAction(label, AccessibilityNodeInfo.ACTION_CLICK, null),
                "no click for a label");
        check(provider.performAction(field, AccessibilityNodeInfo.ACTION_FOCUS, null)
                && asked(FOCUS, 5), "the focus asked for");
        info = provider.createAccessibilityNodeInfo(slider);
        check(offers(info, AccessibilityAction.ACTION_SET_PROGRESS)
                && offers(info, AccessibilityAction.ACTION_SCROLL_FORWARD)
                && !offers(info, AccessibilityAction.ACTION_CLICK), "a range's actions");
        check(provider.performAction(slider, AccessibilityNodeInfo.ACTION_SCROLL_FORWARD, null)
                && asked(INCREMENT, 8)
                && provider.performAction(slider, AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD, null)
                && asked(DECREMENT, 8), "scrolling a range steps it");
        Bundle progress = new Bundle();
        progress.putFloat(AccessibilityNodeInfo.ACTION_ARGUMENT_PROGRESS_VALUE, 55.0f);
        check(provider.performAction(slider, android.R.id.accessibilityActionSetProgress, progress)
                && asked(SET_VALUE, 8) && askedValue() == 55.0f, "a range set");
        check(provider.performAction(box, AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS, null)
                && provider.createAccessibilityNodeInfo(box).isAccessibilityFocused()
                && same(provider.findFocus(AccessibilityNodeInfo.FOCUS_ACCESSIBILITY)
                        .getContentDescription(), "Agree"), "the screen reader's cursor");
        check(provider.performAction(box, AccessibilityNodeInfo.ACTION_CLEAR_ACCESSIBILITY_FOCUS, null)
                && provider.findFocus(AccessibilityNodeInfo.FOCUS_ACCESSIBILITY) == null,
                "the cursor taken away");
        check(same(provider.findFocus(AccessibilityNodeInfo.FOCUS_INPUT).getContentDescription(), "OK"),
                "the input focus");
        maul.ui.AccessProvider ours = (maul.ui.AccessProvider) provider;
        check(ours.virtualViewAt(30.0f, 30.0f) == button
                && ours.virtualViewAt(700.0f, 700.0f) == View.NO_ID, "the node under a place");
    }
}
