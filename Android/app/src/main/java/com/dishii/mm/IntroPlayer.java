package com.dishii.mm;

import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
import android.animation.ValueAnimator;
import android.content.SharedPreferences;
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

// The opening video, played over the game when the app opens, before the title menu. The game boots underneath it
// with its sound muted (nativeSetIntroPlaying), so when the video ends the menu is already there and the picture
// fades from the video to it.
//
// The video is res/raw/abertura.mp4, which android.yml takes from the release "intro-video"; a file abertura.mp4 in
// the data folder replaces it. The subtitles are res/raw/abertura_legendas.srt, or abertura.srt in the data folder.
// The options are the CVars gEnhancements.TitleMenu.Intro, IntroEveryLaunch and IntroSkippable (game menu,
// Enhancements > Time Savers), read from the game's config file because the game has not started yet.
final class IntroPlayer implements TextureView.SurfaceTextureListener {

    private static final String PREF_PLAYED = "introPlayed";
    private static final long FADE_MS = 700;
    private static final long HINT_DELAY_MS = 1200;

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
                if (done) {
                    return;
                }
                mp.start();
                handler.post(tick);
                if (skippable) {
                    hint.animate().alpha(1f).setStartDelay(HINT_DELAY_MS).setDuration(500).start();
                }
            });
            player.setOnCompletionListener(mp -> finish(true));
            player.setOnErrorListener((mp, what, extra) -> {
                MainActivity.startupLog("Abertura: o aparelho não tocou o vídeo (erro " + what + "/" + extra + ")");
                finish(false);
                return true;
            });
            player.prepareAsync();
        } catch (IOException | RuntimeException e) {
            MainActivity.startupLog("Abertura: erro ao abrir o vídeo: " + e);
            finish(false);
        }
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

    // fade: the video and its sound fade out over the menu; otherwise everything goes at once (the app went away,
    // or the video could not be played).
    void finish(boolean fade) {
        if (done || (finishing && fade)) {
            return;
        }
        finishing = true;
        handler.removeCallbacks(tick);
        setGameMuted(false); // the game's sound fades back in on the native side
        if (!fade || root == null) {
            release();
            return;
        }
        subtitle.setVisibility(android.view.View.INVISIBLE);
        hint.animate().cancel();
        hint.setVisibility(android.view.View.GONE);
        ValueAnimator anim = ValueAnimator.ofFloat(1f, 0f);
        anim.setDuration(FADE_MS);
        anim.addUpdateListener(a -> {
            float v = (float) a.getAnimatedValue();
            if (root != null) {
                root.setAlpha(v);
            }
            if (player != null) {
                try {
                    player.setVolume(v, v);
                } catch (IllegalStateException e) {
                    // already stopped
                }
            }
        });
        anim.addListener(new AnimatorListenerAdapter() {
            @Override
            public void onAnimationEnd(Animator animation) {
                release();
            }
        });
        anim.start();
    }

    private void release() {
        if (done) {
            return;
        }
        done = true;
        handler.removeCallbacksAndMessages(null);
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
        if (root != null) {
            ViewGroup parent = (ViewGroup) root.getParent();
            if (parent != null) {
                parent.removeView(root);
            }
            root = null;
        }
        texture = null;
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
