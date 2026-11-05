package os.lumna.remote

import android.Manifest
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import java.util.concurrent.atomic.AtomicInteger

/** Os avisos do PC (e "arquivo recebido") aparecendo no celular. */
object Notifier {
    private const val CHANNEL = "pc"
    private val ids = AtomicInteger(1000)

    fun canPost(context: Context): Boolean =
        Build.VERSION.SDK_INT < 33 || context.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED

    fun show(context: Context, app: String, summary: String, body: String, urgent: Boolean) {
        if (!canPost(context)) return
        val manager = context.getSystemService(NotificationManager::class.java) ?: return
        manager.createNotificationChannel(NotificationChannel(CHANNEL, "Avisos do PC", NotificationManager.IMPORTANCE_DEFAULT))
        val open = PendingIntent.getActivity(
            context, 0, Intent(context, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        val title = if (app.isEmpty()) summary else if (summary.isEmpty()) app else "$app · $summary"
        val notification = android.app.Notification.Builder(context, CHANNEL)
            .setSmallIcon(R.drawable.ic_launcher)
            .setContentTitle(title.ifEmpty { "LUMNA" })
            .setContentText(body)
            .setStyle(android.app.Notification.BigTextStyle().bigText(body))
            .setContentIntent(open)
            .setAutoCancel(true)
            .setCategory(if (urgent) android.app.Notification.CATEGORY_ALARM else android.app.Notification.CATEGORY_MESSAGE)
            .build()
        manager.notify(ids.incrementAndGet(), notification)
    }
}
