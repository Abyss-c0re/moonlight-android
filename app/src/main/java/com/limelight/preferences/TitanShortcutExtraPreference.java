package com.limelight.preferences;

import android.content.Context;
import android.preference.EditTextPreference;
import android.text.InputType;
import android.util.AttributeSet;
import android.view.View;
import android.widget.EditText;

/** Several {@code slot=action} lines. One line is a Controls slot and a chord. */
public final class TitanShortcutExtraPreference extends EditTextPreference {
    public TitanShortcutExtraPreference(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    @Override
    protected void onBindDialogView(View view) {
        super.onBindDialogView(view);
        EditText edit = view.findViewById(android.R.id.edit);
        if (edit == null) return;
        edit.setSingleLine(false);
        edit.setMinLines(6);
        edit.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE);
    }
}
