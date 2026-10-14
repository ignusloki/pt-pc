// P.T. on Android (docs/mobile.md): SDL's activity running the game in libmain.so (SDL is linked into it statically).
package org.ignusloki.pt.mobile;

import android.os.Bundle;
import android.view.WindowManager;
import org.libsdl.app.SDLActivity;

public class PTActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }
}
