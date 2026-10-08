package io.sameboy.android;

import android.graphics.PointF;
import android.graphics.RectF;

/** Console-body geometry for the emulator screen, ported from the iOS port's
 *  GBVerticalLayout/GBHorizontalLayout (points -> dp -> px). All outputs px. */
final class GBLayout {
    final RectF screenRect = new RectF();
    final float bezelWidth;
    final PointF dpad = new PointF(), a = new PointF(), b = new PointF();
    final PointF select = new PointF(), start = new PointF();
    final PointF rewind = new PointF(), turbo = new PointF(), menu = new PointF();
    final boolean landscape;
    boolean compact = false;
    boolean drawLogo = false;
    float logoY, logoSize;

    /** srcW/srcH: the Core's current output size — 160x144, or 256x224 while an SGB
     *  border is displayed. All screen math is in units of it, so a border widens the
     *  well instead of being letterboxed inside a 160:144 one. */
    GBLayout(int w, int h, float dp, boolean fullscreen, int srcW, int srcH) {
        final float unitW = srcW, unitH = srcH;
        landscape = w > h;
        if (fullscreen) {
            // Controls hidden: the whole display goes to the screen — largest centered
            // aspect-correct fit, no console body. Control points stay unused (0,0).
            float s = Math.min(w / unitW, h / unitH);
            float sw = unitW * s, sh = unitH * s;
            screenRect.set((w - sw) / 2f, (h - sh) / 2f, (w + sw) / 2f, (h + sh) / 2f);
            bezelWidth = 0f;
            return;
        }
        if (landscape) {
            // height-filling integer-scaled screen, >=164dp wings for controls
            float sh = (float) Math.floor(h / unitH) * unitH;
            if (sh == 0) sh = h;
            float sw = sh / unitH * unitW;
            while ((w - sw) / 2f < 164f * dp && sh > unitH) { sh -= unitH; sw -= unitW; }
            float border = Math.min(sw / 40f, 16f * dp);
            float x = (w - sw) / 2f;
            float vMargin = (h - sh) / 2f;
            float y;
            if (vMargin * 2f > border * 7f) { drawLogo = true; y = (h - sh - border * 5f) / 2f; }
            else y = vMargin;
            screenRect.set(x, y, x + sw, y + sh);
            bezelWidth = border;
            if (drawLogo) { logoSize = 24f * dp; logoY = screenRect.bottom + border * 2f; }

            dpad.set(Math.round((x - border) / 2f), Math.round(h * 3f / 8f));
            float buttonsCx = (w + sw + x) / 2f;
            PointF delta = buttonDelta((w - sw) / 2f - border * 5f - 36f * dp * 2f, dp);
            a.set(Math.round(buttonsCx + delta.x / 2f), Math.round(dpad.y - delta.y / 2f));
            b.set(Math.round(buttonsCx - delta.x / 2f), Math.round(dpad.y + delta.y / 2f));
            select.set(dpad.x, Math.min(Math.round(h * 3f / 4f), dpad.y + 180f * dp));
            start.set(buttonsCx, select.y);
            rewind.set(select.x, Math.min(h - 28f * dp, select.y + 64f * dp));
            turbo.set(start.x, rewind.y);
            menu.set(w - 40f * dp, 40f * dp);
        } else {
            // Width-filling integer-scaled screen, top-anchored. Two-pass fit:
            // 1) roomy iOS spacing (tall phones/tablets); 2) compact spacing for
            // short screens (menu pill moves top-right, rewind/turbo under
            // select/start, tighter gaps) — keeps the largest possible screen.
            // Only if even compact overflows does the screen shrink a step.
            float sw = (float) Math.floor(w / unitW) * unitW;
            if (sw == 0) sw = w;
            float sh, border, topInset, y, controlAreaStart, selectY, dpadY;
            while (true) {
                sh = sw / unitW * unitH;
                border = Math.min(sw / 40f, 16f * dp);
                topInset = Math.min(border * 2f, 20f * dp);
                y = topInset + 24f * dp;               // status-bar breathing room
                controlAreaStart = y + sh + topInset;
                float wellClear = y + sh + border;
                // pass 1: roomy (iOS formulas)
                selectY = Math.min(h - 80f * dp, (h - controlAreaStart) * 0.75f + controlAreaStart);
                dpadY = selectY - 140f * dp;
                if (dpadY - 84f * dp >= wellClear || sw <= unitW) { compact = false; break; }
                // pass 2: compact — pack the stack right under the well
                dpadY = wellClear + 84f * dp;          // d-pad half (76dp) + 8dp gap
                selectY = dpadY + 122f * dp;
                float rewindY = selectY + 56f * dp;
                if (rewindY <= h - 20f * dp || sw <= unitW) { compact = true; break; }
                sw -= unitW;
            }
            float x = (w - sw) / 2f;
            screenRect.set(x, y, x + sw, y + sh);
            bezelWidth = border;

            select.set(Math.min(w / 4f, 120f * dp), selectY);
            start.set(w - select.x, select.y);
            dpad.set(select.x, dpadY);
            float buttonsCx = w - dpad.x;
            PointF delta = buttonDelta(w / 2f - 36f * dp * 2f - border * 2f, dp);
            a.set(Math.round(buttonsCx + delta.x / 2f), Math.round(dpad.y - delta.y / 2f));
            b.set(Math.round(buttonsCx - delta.x / 2f), Math.round(dpad.y + delta.y / 2f));

            float controlsTop = dpad.y - 80f * dp;
            if (controlsTop - controlAreaStart > 24f * dp + border * 2f) {
                drawLogo = true; logoSize = 24f * dp; logoY = controlAreaStart + border;
            }
            if (compact) {
                // menu pill centered between Select and Start; rewind/turbo below them
                menu.set(w / 2f, selectY);
                float pillY = Math.min(h - 20f * dp, select.y + 56f * dp);
                rewind.set(select.x, pillY);
                turbo.set(start.x, pillY);
            } else {
                // [<<] [=] [>>] pill row centered between Select and Start
                float pillY = Math.min(h - 24f * dp, select.y + 56f * dp);
                rewind.set(w / 2f - 64f * dp, pillY);
                menu.set(w / 2f, pillY);
                turbo.set(w / 2f + 64f * dp, pillY);
            }
        }
    }

    /** iOS buttonDeltaForMaxHorizontalDistance: default 90x45dp, squeezed onto a 100dp circle. */
    private static PointF buttonDelta(float maxDist, float dp) {
        float wd = 90f * dp, hd = 45f * dp;
        if (wd <= maxDist) return new PointF(wd, hd);
        float w = Math.max(maxDist, 40f * dp);
        float r = 100f * dp;
        return new PointF(w, (float) Math.floor(Math.sqrt(Math.max(0, r * r - w * w))));
    }
}
