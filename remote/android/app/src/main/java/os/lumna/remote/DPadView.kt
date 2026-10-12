package os.lumna.remote

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import kotlin.math.atan2
import kotlin.math.hypot
import kotlin.math.min

/**
 * Direcional redondo com OK no meio, como o de um controle de TV. Segurar uma direção repete,
 * igual ao controle de videogame no LUMNA.
 */
class DPadView(context: Context, private val onAction: (String) -> Unit) : View(context) {
    private val ring = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#1A1433") }
    private val pressed = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#3B2A6E") }
    private val center = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#8B5CF6") }
    private val line = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#2E2552")
        style = Paint.Style.STROKE
        strokeWidth = 3f
    }
    private val arrow = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#ECE8FF") }
    private val label = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textAlign = Paint.Align.CENTER
        isFakeBoldText = true
    }

    private var active: String? = null
    private val repeat = object : Runnable {
        override fun run() {
            val action = active ?: return
            fire(action)
            postDelayed(this, 110)
        }
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val size = MeasureSpec.getSize(widthMeasureSpec)
        setMeasuredDimension(size, size)
    }

    private fun zone(x: Float, y: Float): String? {
        val cx = width / 2f
        val cy = height / 2f
        val r = min(width, height) / 2f
        val d = hypot(x - cx, y - cy)
        if (d > r) return null
        if (d < r * 0.36f) return "accept"
        val angle = Math.toDegrees(atan2((y - cy).toDouble(), (x - cx).toDouble()))
        return when {
            angle in -45.0..45.0 -> "right"
            angle in 45.0..135.0 -> "down"
            angle in -135.0..-45.0 -> "up"
            else -> "left"
        }
    }

    private fun fire(action: String) {
        performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
        onAction(action)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                active = zone(event.x, event.y) ?: return false
                fire(active!!)
                if (active != "accept") postDelayed(repeat, 380)
                invalidate()
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                removeCallbacks(repeat)
                active = null
                invalidate()
                if (event.actionMasked == MotionEvent.ACTION_UP) performClick()
            }
        }
        return true
    }

    override fun performClick(): Boolean = super.performClick()

    override fun onDraw(canvas: Canvas) {
        val cx = width / 2f
        val cy = height / 2f
        val r = min(width, height) / 2f - 4f
        val oval = RectF(cx - r, cy - r, cx + r, cy + r)
        canvas.drawCircle(cx, cy, r, ring)
        // Quadrante apertado em destaque.
        val sweep = mapOf("right" to -45f, "down" to 45f, "left" to 135f, "up" to 225f)
        sweep[active]?.let { start -> canvas.drawArc(oval, start, 90f, true, pressed) }
        for (a in listOf(45f, 135f)) {
            canvas.save()
            canvas.rotate(a, cx, cy)
            canvas.drawLine(cx - r, cy, cx + r, cy, line)
            canvas.restore()
        }
        // Setas.
        val s = r * 0.09f
        for ((dir, dx, dy) in listOf(Triple(0, 0f, -1f), Triple(1, 1f, 0f), Triple(2, 0f, 1f), Triple(3, -1f, 0f))) {
            val ax = cx + dx * r * 0.7f
            val ay = cy + dy * r * 0.7f
            val p = Path()
            when (dir) {
                0 -> { p.moveTo(ax, ay - s); p.lineTo(ax - s, ay + s * 0.6f); p.lineTo(ax + s, ay + s * 0.6f) }
                1 -> { p.moveTo(ax + s, ay); p.lineTo(ax - s * 0.6f, ay - s); p.lineTo(ax - s * 0.6f, ay + s) }
                2 -> { p.moveTo(ax, ay + s); p.lineTo(ax - s, ay - s * 0.6f); p.lineTo(ax + s, ay - s * 0.6f) }
                else -> { p.moveTo(ax - s, ay); p.lineTo(ax + s * 0.6f, ay - s); p.lineTo(ax + s * 0.6f, ay + s) }
            }
            p.close()
            canvas.drawPath(p, arrow)
        }
        canvas.drawCircle(cx, cy, r * 0.34f, if (active == "accept") pressed else center)
        label.textSize = r * 0.16f
        canvas.drawText("OK", cx, cy + label.textSize * 0.35f, label)
    }
}
