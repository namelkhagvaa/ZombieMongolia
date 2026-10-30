package mn.zombiemongolia.game;

import android.app.NativeActivity;
import android.content.Context;
import android.graphics.Rect;
import android.os.Build;
import android.os.Bundle;
import android.text.Editable;
import android.text.InputFilter;
import android.text.InputType;
import android.text.TextWatcher;
import android.view.View;
import android.view.WindowInsets;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.FrameLayout;

// Утасны өөрийн гар (src/Platform.cpp: NativeFieldOpen, WebFieldPoll, WebFieldBlur, WebVisibleFraction).
// NativeActivity-ийн дээр үл үзэгдэх нэг EditText: тоглоом талбар товшиход түүнийг идэвхжүүлж гарыг нээнэ, бичсэнийг
// кадр бүр уншина. Үг таамаглах, автомат засвар, гарын "бүтэн дэлгэцийн" засварлагчийг хаана.
public class GameActivity extends NativeActivity {
    private EditText field;
    private volatile int slot = -1;
    private volatile String text = "";
    private volatile boolean enter = false;
    private volatile float visible = 1.0f;
    private boolean filling = false; // (тоглоомоос утга хуулж байна: TextWatcher-ийг тоолохгүй)

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        field = new EditText(this);
        field.setAlpha(0.0f);
        field.setBackground(null);
        field.setCursorVisible(false);
        field.setFocusableInTouchMode(true);
        field.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) {}
            @Override public void afterTextChanged(Editable s) {
                if (!filling) text = s.toString();
            }
        });
        field.setOnEditorActionListener((view, action, event) -> {
            enter = true; // "Дараах" эсвэл "Болсон": тоглоом дараагийн талбар руу шилжинэ, эсвэл илгээнэ
            return true;
        });
        addContentView(field, new FrameLayout.LayoutParams(1, 1));
        // Гар нээгдэхэд дэлгэцийн харагдах хэсэг (тоглоом бичиж буй талбараа дээш шилжүүлнэ). Бүтэн дэлгэцийн
        // цонхонд getWindowVisibleDisplayFrame гарыг тоолдоггүй тул Android 11+ дээр гарын inset-ийг шууд уншина.
        final View root = getWindow().getDecorView();
        root.getViewTreeObserver().addOnGlobalLayoutListener(() -> {
            int height = root.getHeight();
            if (height <= 0) return;
            int covered;
            WindowInsets insets = root.getRootWindowInsets();
            if (Build.VERSION.SDK_INT >= 30 && insets != null) {
                covered = insets.getInsets(WindowInsets.Type.ime()).bottom;
            } else {
                Rect frame = new Rect();
                root.getWindowVisibleDisplayFrame(frame);
                covered = height - frame.bottom;
            }
            visible = Math.max(0.2f, Math.min(1.0f, (float) (height - covered) / height));
        });
    }

    // kind: 0 текст, 1 утасны дугаар, 2 нууц үг, 3 тоо, 4 чат. next >= 0 бол гарын товч "Дараах", эс бөгөөс "Болсон".
    public void zmOpen(final int newSlot, final int kind, final int next, final String value) {
        slot = newSlot;
        text = value;
        enter = false;
        runOnUiThread(() -> {
            int type;
            int maxLength;
            if (kind == 1 || kind == 3) {
                type = InputType.TYPE_CLASS_NUMBER;
                maxLength = kind == 1 ? 8 : 20;
            } else if (kind == 2) {
                // Нууц үг: латин гар, санал, засваргүй (тоглоом өөрөө "•"-ээр харуулна)
                type = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS;
                maxLength = 32;
            } else {
                // Нэр, чат: кирилл гар хэвээр, үг таамаглах, автомат засваргүй
                type = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS;
                maxLength = kind == 4 ? 120 : 30;
            }
            filling = true;
            field.setInputType(type);
            field.setFilters(new InputFilter[] { new InputFilter.LengthFilter(maxLength) });
            field.setImeOptions((next >= 0 ? EditorInfo.IME_ACTION_NEXT : EditorInfo.IME_ACTION_DONE)
                                | EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_FLAG_NO_FULLSCREEN);
            field.setText(value);
            field.setSelection(field.getText().length());
            filling = false;
            field.requestFocus();
            InputMethodManager manager = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
            manager.restartInput(field);
            manager.showSoftInput(field, InputMethodManager.SHOW_IMPLICIT);
        });
    }

    public void zmClose() {
        slot = -1;
        runOnUiThread(() -> {
            InputMethodManager manager = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
            manager.hideSoftInputFromWindow(field.getWindowToken(), 0);
            field.clearFocus();
        });
    }

    public int zmSlot() { return slot; }
    public String zmText() { return text; }
    public boolean zmTakeEnter() {
        boolean pressed = enter;
        enter = false;
        return pressed;
    }
    public float zmVisible() { return visible; }
}
