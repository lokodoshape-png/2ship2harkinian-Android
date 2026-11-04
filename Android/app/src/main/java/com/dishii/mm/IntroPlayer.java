package com.dishii.mm;

import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
import android.animation.ValueAnimator;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.graphics.SurfaceTexture;
import android.graphics.Typeface;
import android.media.AudioAttributes;
import android.media.MediaPlayer;
import android.net.Uri;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.TextureView;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.TextView;

import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

// The opening, played over the game when the app opens, before the title menu: the edition's logo (assets/intro/
// logo.jpg) fades in from black, stays, fades out, and the video starts. The video is made ready while the logo is up,
// so it starts as soon as the logo is gone. Every way out (the end, a skip, an error) goes through the moon: its red
// eyes burn, pulse and go out, and the title menu fades in (exitToMenu). The game boots underneath it
// with its sound muted (nativeSetIntroPlaying), so the menu is already there when the moon's eyes go out.
//
// The video is res/raw/abertura.mp4, which android.yml takes from the release "intro-video"; a file abertura.mp4 in
// the data folder replaces it. The subtitles are res/raw/abertura_legendas.srt, or abertura.srt in the data folder.
// The options are the CVars gEnhancements.TitleMenu.Intro, IntroEveryLaunch and IntroSkippable (game menu,
// Enhancements > Time Savers), read from the game's config file because the game has not started yet.
final class IntroPlayer implements TextureView.SurfaceTextureListener {

    private static final String PREF_PLAYED = "introPlayed";
    private static final long HINT_DELAY_MS = 1200;
    private static final String LOGO_ASSET = "intro/logo.jpg";
    private static final long LOGO_IN_MS = 1500, LOGO_HOLD_MS = 3000, LOGO_OUT_MS = 1000;

    private final MainActivity activity;
    private final boolean skippable;
    private final Uri rawVideo;
    private final File fileVideo;
    private final List<Cue> cues;
    private final Handler handler = new Handler(Looper.getMainLooper());

    private FrameLayout root;
    private TextureView texture;
    private TextView subtitle;
    private TextView hint;
    private ImageView logo;
    private boolean prepared;  // the video is ready to start
    private boolean logoDone;  // the logo has faded out (or there is none)
    private boolean started;
    private MediaPlayer player;
    private Surface surface;
    private int videoW, videoH;
    private boolean finishing;
    private boolean done;

    private static final class Cue {
        final int start, end;
        final String text;
        Cue(int start, int end, String text) {
            this.start = start;
            this.end = end;
            this.text = text;
        }
    }

    // Starts the video when it is on and there is one; returns null otherwise.
    static IntroPlayer startIfWanted(MainActivity activity, SharedPreferences prefs) {
        File data = new File(Environment.getExternalStorageDirectory(), MainActivity.DATA_FOLDER);
        // The first launch (no game data yet) shows the setup and ROM steps, which the video would cover.
        if (!new File(data, "mm.o2r").isFile()) {
            MainActivity.startupLog("Abertura: sem mm.o2r ainda, não toca");
            return null;
        }
        JSONObject options = titleMenuOptions(new File(data, "2ship2harkinian.json"));
        boolean enabled = options.optInt("Intro", 1) != 0;
        boolean everyLaunch = options.optInt("IntroEveryLaunch", 1) != 0;
        boolean skippable = options.optInt("IntroSkippable", 1) != 0;
        if (!enabled || (!everyLaunch && prefs.getBoolean(PREF_PLAYED, false))) {
            MainActivity.startupLog("Abertura: desligada nas opções");
            return null;
        }
        File file = new File(data, "abertura.mp4");
        Uri raw = null;
        if (!file.isFile()) {
            file = null;
            int id = activity.getResources().getIdentifier("abertura", "raw", activity.getPackageName());
            if (id == 0) {
                MainActivity.startupLog("Abertura: este APK não tem o vídeo");
                return null;
            }
            raw = Uri.parse("android.resource://" + activity.getPackageName() + "/" + id);
        }
        prefs.edit().putBoolean(PREF_PLAYED, true).apply();
        IntroPlayer intro = new IntroPlayer(activity, skippable, raw, file, loadCues(activity, data));
        intro.show();
        return intro;
    }

    private IntroPlayer(MainActivity activity, boolean skippable, Uri rawVideo, File fileVideo, List<Cue> cues) {
        this.activity = activity;
        this.skippable = skippable;
        this.rawVideo = rawVideo;
        this.fileVideo = fileVideo;
        this.cues = cues;
    }

    boolean isActive() {
        return !done;
    }

    // ----- Options and subtitles

    private static JSONObject titleMenuOptions(File config) {
        try {
            JSONObject json = new JSONObject(readAll(new FileInputStream(config)));
            JSONObject menu = json.optJSONObject("CVars");
            menu = menu != null ? menu.optJSONObject("gEnhancements") : null;
            menu = menu != null ? menu.optJSONObject("TitleMenu") : null;
            return menu != null ? menu : new JSONObject();
        } catch (Exception e) {
            return new JSONObject(); // no config yet: the defaults
        }
    }

    private static List<Cue> loadCues(MainActivity activity, File data) {
        String srt = null;
        try {
            File file = new File(data, "abertura.srt");
            if (file.isFile()) {
                srt = readAll(new FileInputStream(file));
            } else {
                int id = activity.getResources().getIdentifier("abertura_legendas", "raw", activity.getPackageName());
                if (id != 0) {
                    srt = readAll(activity.getResources().openRawResource(id));
                }
            }
        } catch (IOException e) {
            MainActivity.startupLog("Abertura: erro ao ler as legendas: " + e);
        }
        List<Cue> cues = new ArrayList<>();
        if (srt == null) {
            return cues;
        }
        Pattern time = Pattern.compile(
                "(\\d+):(\\d+):(\\d+)[,.](\\d+)\\s*-->\\s*(\\d+):(\\d+):(\\d+)[,.](\\d+)");
        for (String block : srt.replace("\r", "").split("\n\\s*\n")) {
            String[] lines = block.trim().split("\n");
            for (int i = 0; i < lines.length; i++) {
                Matcher m = time.matcher(lines[i]);
                if (!m.find()) {
                    continue;
                }
                StringBuilder text = new StringBuilder();
                for (int j = i + 1; j < lines.length; j++) {
                    text.append(text.length() > 0 ? "\n" : "").append(lines[j].trim());
                }
                if (text.length() > 0) {
                    cues.add(new Cue(ms(m, 1), ms(m, 5), text.toString()));
                }
                break;
            }
        }
        return cues;
    }

    private static int ms(Matcher m, int g) {
        return Integer.parseInt(m.group(g)) * 3600000 + Integer.parseInt(m.group(g + 1)) * 60000
                + Integer.parseInt(m.group(g + 2)) * 1000 + Integer.parseInt(m.group(g + 3));
    }

    private static String readAll(InputStream in) throws IOException {
        StringBuilder out = new StringBuilder();
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(in, StandardCharsets.UTF_8))) {
            char[] buf = new char[8192];
            int n;
            while ((n = reader.read(buf)) > 0) {
                out.append(buf, 0, n);
            }
        }
        return out.toString().replace("﻿", "");
    }

    // ----- Views

    private float dp(float v) {
        return TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, activity.getResources().getDisplayMetrics());
    }

    private void show() {
        setGameMuted(true);
        root = new FrameLayout(activity);
        root.setBackgroundColor(Color.BLACK);
        root.setClickable(true);
        root.setFocusable(false);
        root.setOnTouchListener((v, e) -> {
            if (e.getActionMasked() == MotionEvent.ACTION_UP) {
                skip();
            }
            return true; // nothing reaches the game or the touch controls under the video
        });

        texture = new TextureView(activity);
        texture.setSurfaceTextureListener(this);
        root.addView(texture, new FrameLayout.LayoutParams(1, 1, Gravity.CENTER));

        Bitmap logoBitmap = null;
        try (InputStream in = activity.getAssets().open(LOGO_ASSET)) {
            logoBitmap = BitmapFactory.decodeStream(in);
        } catch (IOException e) {
            MainActivity.startupLog("Abertura: sem a logo, vai direto para o vídeo");
        }
        if (logoBitmap != null) {
            logo = new ImageView(activity);
            logo.setImageBitmap(logoBitmap);
            logo.setScaleType(ImageView.ScaleType.FIT_CENTER); // the whole picture, at its own proportions
            logo.setAlpha(0f);
            root.addView(logo, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT));
        } else {
            logoDone = true;
        }

        Typeface font = null;
        try {
            font = Typeface.createFromAsset(activity.getAssets(), "fonts/Alegreya-Bold.ttf");
        } catch (RuntimeException e) {
            font = Typeface.SERIF;
        }

        subtitle = new TextView(activity);
        subtitle.setTypeface(font);
        subtitle.setTextColor(Color.WHITE);
        subtitle.setTextSize(TypedValue.COMPLEX_UNIT_SP, 19);
        subtitle.setGravity(Gravity.CENTER);
        subtitle.setShadowLayer(dp(3), 0, dp(1), Color.BLACK);
        subtitle.setLineSpacing(0, 1.1f);
        subtitle.setPadding((int) dp(12), (int) dp(4), (int) dp(12), (int) dp(4));
        subtitle.setBackgroundColor(Color.argb(90, 0, 0, 0));
        subtitle.setVisibility(android.view.View.INVISIBLE);
        FrameLayout.LayoutParams sp = new FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL);
        sp.bottomMargin = (int) dp(28);
        root.addView(subtitle, sp);

        hint = new TextView(activity);
        hint.setTypeface(font);
        hint.setText("Pressione qualquer botão para pular");
        hint.setTextColor(Color.argb(200, 235, 225, 245));
        hint.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        hint.setShadowLayer(dp(2), 0, 0, Color.BLACK);
        hint.setAlpha(0f);
        FrameLayout.LayoutParams hp = new FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM | Gravity.END);
        hp.bottomMargin = (int) dp(10);
        hp.rightMargin = (int) dp(16);
        root.addView(hint, hp);
        if (!skippable) {
            hint.setVisibility(android.view.View.GONE);
        }

        if (skippable) {
            hint.animate().alpha(1f).setStartDelay(HINT_DELAY_MS).setDuration(500).start();
        }
        if (logo != null) {
            logo.animate().alpha(1f).setDuration(LOGO_IN_MS).withEndAction(() ->
                    handler.postDelayed(() -> {
                        if (done || logo == null) {
                            return;
                        }
                        logo.animate().alpha(0f).setDuration(LOGO_OUT_MS).withEndAction(() -> {
                            if (root != null && logo != null) {
                                root.removeView(logo);
                            }
                            logo = null;
                            logoDone = true;
                            startVideoWhenReady();
                        }).start();
                    }, LOGO_HOLD_MS)).start();
        }
        root.addOnLayoutChangeListener((v, l, t, r, b, ol, ot, or, ob) -> fitVideo());
        ((ViewGroup) activity.getContentView()).addView(root,
                new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        MainActivity.startupLog("Abertura: começando (" + (fileVideo != null ? fileVideo : "APK") + ", " + cues.size()
                + " legendas)");
    }

    // The whole video on the screen at its own aspect ratio, black bars where the shapes differ.
    private void fitVideo() {
        if (texture == null || root == null || videoW <= 0 || videoH <= 0 || root.getWidth() == 0) {
            return;
        }
        float scale = Math.min(root.getWidth() / (float) videoW, root.getHeight() / (float) videoH);
        int w = Math.round(videoW * scale), h = Math.round(videoH * scale);
        FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams) texture.getLayoutParams();
        if (lp.width != w || lp.height != h) {
            lp.width = w;
            lp.height = h;
            texture.post(() -> texture.setLayoutParams(lp));
        }
    }

    // ----- Playback

    @Override
    public void onSurfaceTextureAvailable(SurfaceTexture st, int width, int height) {
        if (done || player != null) {
            return;
        }
        surface = new Surface(st);
        player = new MediaPlayer();
        try {
            player.setAudioAttributes(new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MOVIE)
                    .build());
            if (fileVideo != null) {
                player.setDataSource(fileVideo.getAbsolutePath());
            } else {
                player.setDataSource(activity, rawVideo);
            }
            player.setSurface(surface);
            player.setOnVideoSizeChangedListener((mp, w, h) -> {
                videoW = w;
                videoH = h;
                fitVideo();
            });
            player.setOnPreparedListener(mp -> {
                prepared = true;
                startVideoWhenReady();
            });
            player.setOnCompletionListener(mp -> finish(true));
            player.setOnErrorListener((mp, what, extra) -> {
                MainActivity.startupLog("Abertura: o aparelho não tocou o vídeo (erro " + what + "/" + extra + ")");
                finish(true);
                return true;
            });
            player.prepareAsync();
        } catch (IOException | RuntimeException e) {
            MainActivity.startupLog("Abertura: erro ao abrir o vídeo: " + e);
            finish(true);
        }
    }

    // The video starts once it is ready and the logo is gone, whichever comes last.
    private void startVideoWhenReady() {
        if (done || finishing || started || !prepared || !logoDone || player == null) {
            return;
        }
        started = true;
        player.start();
        handler.post(tick);
    }

    // Shows the subtitle of the moment.
    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            if (done || player == null) {
                return;
            }
            String text = null;
            try {
                int now = player.getCurrentPosition();
                for (Cue c : cues) {
                    if (now >= c.start && now < c.end) {
                        text = c.text;
                        break;
                    }
                }
            } catch (IllegalStateException e) {
                return;
            }
            if (text == null) {
                subtitle.setVisibility(android.view.View.INVISIBLE);
            } else {
                if (!text.contentEquals(subtitle.getText())) {
                    subtitle.setText(text);
                }
                subtitle.setVisibility(android.view.View.VISIBLE);
            }
            handler.postDelayed(this, 50);
        }
    };

    @Override
    public void onSurfaceTextureSizeChanged(SurfaceTexture st, int width, int height) {
    }

    @Override
    public boolean onSurfaceTextureDestroyed(SurfaceTexture st) {
        return true;
    }

    @Override
    public void onSurfaceTextureUpdated(SurfaceTexture st) {
    }

    // ----- Input: any button or a tap skips; nothing reaches the game while the video is up.

    boolean onKey(KeyEvent event) {
        int code = event.getKeyCode();
        if (code == KeyEvent.KEYCODE_VOLUME_UP || code == KeyEvent.KEYCODE_VOLUME_DOWN
                || code == KeyEvent.KEYCODE_VOLUME_MUTE) {
            return false;
        }
        if (event.getAction() == KeyEvent.ACTION_UP) {
            skip();
        }
        return true;
    }

    private void skip() {
        if (skippable) {
            MainActivity.startupLog("Abertura: pulada");
            finish(true);
        }
    }

    // ----- End

    // Every way out of the opening comes here: the video ended, a button or a tap skipped it (during the logo too),
    // or it could not be played. animate false only when the app goes away (nothing would be seen).
    void finish(boolean animate) {
        if (done) {
            return;
        }
        if (!animate || root == null) {
            finishing = true;
            setGameMuted(false);
            release();
            return;
        }
        exitToMenu();
    }

    // The one way into the title menu: the video is stopped and let go, the screen goes dark, the moon looks out
    // with its red eyes burning and pulsing, they dim, and the menu fades in once they are out. Runs only once.
    private void exitToMenu() {
        if (finishing || done) {
            return;
        }
        finishing = true;
        handler.removeCallbacksAndMessages(null); // the subtitles and the logo's own timing
        releasePlayer();
        if (texture != null) {
            root.removeView(texture);
            texture = null;
        }
        if (logo != null) {
            logo.animate().cancel();
            root.removeView(logo);
            logo = null;
        }
        subtitle.setVisibility(android.view.View.GONE);
        hint.animate().cancel();
        hint.setVisibility(android.view.View.GONE);
        root.setAlpha(1f);
        root.setBackgroundColor(Color.TRANSPARENT); // the moon's own view draws the dark from here on

        final MoonView moon = new MoonView(activity);
        root.addView(moon, 0, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        ValueAnimator anim = ValueAnimator.ofFloat(0f, MoonView.TOTAL);
        anim.setDuration((long) (MoonView.TOTAL * 1000));
        anim.setInterpolator(null); // linear: the view shapes the timing itself
        anim.addUpdateListener(a -> {
            moon.t = (float) a.getAnimatedValue();
            if (moon.t >= MoonView.REVEAL) {
                setGameMuted(false); // the menu's sound fades in with its picture
            }
            moon.invalidate();
        });
        anim.addListener(new AnimatorListenerAdapter() {
            @Override
            public void onAnimationEnd(Animator animation) {
                setGameMuted(false);
                release();
            }
        });
        MainActivity.startupLog("Abertura: transição da lua");
        anim.start();
    }

    // The moon close up (assets/intro/moon.png, from the title screen's art) and the light of its eyes.
    private static final class MoonView extends android.view.View {
        static final float TOTAL = 4.6f;   // seconds
        static final float REVEAL = 3.9f;  // the dark starts to lift off the menu
        // The eyes in moon.png (840 x 300).
        static final float[][] EYES = { { 232f, 165f }, { 604f, 17f } };

        float t = 0f;
        private final Bitmap bitmap;
        private final android.graphics.Paint paint = new android.graphics.Paint(android.graphics.Paint.FILTER_BITMAP_FLAG);
        private final android.graphics.Paint glow = new android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG);
        private final android.graphics.RectF dst = new android.graphics.RectF();

        MoonView(MainActivity activity) {
            super(activity);
            Bitmap b = null;
            try (InputStream in = activity.getAssets().open("intro/moon.png")) {
                b = BitmapFactory.decodeStream(in);
            } catch (IOException e) {
                MainActivity.startupLog("Abertura: sem a lua da transição");
            }
            bitmap = b;
        }

        private static float clamp01(float v) {
            return Math.max(0f, Math.min(1f, v));
        }

        private static float smooth(float v) {
            v = clamp01(v);
            return v * v * (3f - 2f * v);
        }

        private void radial(android.graphics.Canvas c, float x, float y, float r, int inner, int outer) {
            glow.setShader(new android.graphics.RadialGradient(x, y, r, new int[] { inner, outer, Color.TRANSPARENT },
                    new float[] { 0f, 0.35f, 1f }, android.graphics.Shader.TileMode.CLAMP));
            c.drawCircle(x, y, r, glow);
        }

        private static int argb(float a, int r, int g, int b) {
            return Color.argb(Math.round(255 * clamp01(a)), r, g, b);
        }

        @Override
        protected void onDraw(android.graphics.Canvas c) {
            int w = getWidth(), h = getHeight();
            // The dark: whole until REVEAL, then it lifts off the menu.
            float cover = 1f - smooth((t - REVEAL) / (TOTAL - REVEAL));
            c.drawColor(argb(cover, 0, 0, 0));
            if (bitmap == null || t >= REVEAL) {
                return;
            }
            float moonA = smooth((t - 0.4f) / 1.0f) * (1f - smooth((t - 3.2f) / 0.7f));
            // Eyes: they kindle, burn and pulse, then go out.
            float eyes = smooth((t - 1.0f) / 0.8f) * (1f - smooth((t - 2.8f) / 0.9f));
            float pulse = 0.72f + 0.28f * (float) Math.sin(t * Math.PI * 2 * 1.6f);
            // The moon fills most of the width, drifting closer the whole time.
            float zoom = 1f + 0.06f * clamp01(t / TOTAL);
            float scale = Math.min(w * 0.96f / bitmap.getWidth(), h * 0.9f / bitmap.getHeight()) * zoom;
            float bw = bitmap.getWidth() * scale, bh = bitmap.getHeight() * scale;
            float x0 = (w - bw) / 2, y0 = (h - bh) / 2;
            // A violet haze behind the moon.
            radial(c, w / 2f, h / 2f, Math.max(bw, bh) * 0.7f, argb(0.45f * moonA * (0.6f + 0.4f * eyes), 120, 40, 200),
                    argb(0.25f * moonA * (0.6f + 0.4f * eyes), 70, 20, 140));
            dst.set(x0, y0, x0 + bw, y0 + bh);
            paint.setAlpha(Math.round(255 * moonA));
            c.drawBitmap(bitmap, null, dst, paint);
            if (eyes <= 0f) {
                return;
            }
            float g = eyes * pulse;
            for (float[] e : EYES) {
                float ex = x0 + e[0] * scale, ey = y0 + e[1] * scale;
                float r = bh * 0.5f;
                radial(c, ex, ey, r * 1.6f, argb(0.35f * g, 170, 60, 255), argb(0.18f * g, 110, 30, 200)); // violet
                radial(c, ex, ey, r, argb(0.85f * g, 255, 40, 20), argb(0.45f * g, 200, 0, 0));            // red
                radial(c, ex, ey, r * 0.22f, argb(0.95f * g, 255, 210, 150), argb(0.7f * g, 255, 90, 30)); // core
            }
        }
    }

    private void releasePlayer() {
        if (player != null) {
            try {
                player.stop();
            } catch (IllegalStateException e) {
                // was not playing
            }
            player.release();
            player = null;
        }
        if (surface != null) {
            surface.release();
            surface = null;
        }
    }

    private void release() {
        if (done) {
            return;
        }
        done = true;
        handler.removeCallbacksAndMessages(null);
        releasePlayer();
        if (root != null) {
            ViewGroup parent = (ViewGroup) root.getParent();
            if (parent != null) {
                parent.removeView(root);
            }
            root = null;
        }
        texture = null;
        if (logo != null) {
            logo.animate().cancel();
            logo = null;
        }
        MainActivity.startupLog("Abertura: terminou");
    }

    private static void setGameMuted(boolean muted) {
        try {
            MainActivity.nativeSetIntroPlaying(muted);
        } catch (UnsatisfiedLinkError e) {
            // the game library is not loaded (cannot happen after SDLActivity.onCreate)
        }
    }
}
