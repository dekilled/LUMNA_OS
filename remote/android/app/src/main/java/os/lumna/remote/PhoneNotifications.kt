package os.lumna.remote

import android.app.Notification
import android.os.Handler
import android.os.Looper
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification

/** Os avisos do celular indo para o PC. O usuário liga em Ajustes do Android > Acesso às notificações. */
class PhoneNotifications : NotificationListenerService() {
    private val main = Handler(Looper.getMainLooper())

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        Connect.init(this)
        val n = sbn.notification
        if (sbn.packageName == packageName || sbn.isOngoing) return
        if (n.flags and Notification.FLAG_GROUP_SUMMARY != 0) return
        val extras = n.extras ?: return
        val title = extras.getCharSequence(Notification.EXTRA_TITLE)?.toString().orEmpty()
        val text = (extras.getCharSequence(Notification.EXTRA_BIG_TEXT) ?: extras.getCharSequence(Notification.EXTRA_TEXT))?.toString().orEmpty()
        if (title.isEmpty() && text.isEmpty()) return
        val app = runCatching {
            packageManager.getApplicationLabel(packageManager.getApplicationInfo(sbn.packageName, 0)).toString()
        }.getOrDefault(sbn.packageName.substringAfterLast('.'))
        val urgent = n.category == Notification.CATEGORY_CALL || n.category == Notification.CATEGORY_ALARM
        main.post { Connect.sendPhoneNotification(app, title, text.take(500), urgent) }
    }

    companion object {
        /** O usuário deu acesso às notificações a este app? */
        fun allowed(context: android.content.Context): Boolean {
            val enabled = android.provider.Settings.Secure.getString(context.contentResolver, "enabled_notification_listeners") ?: return false
            return enabled.split(":").any { it.startsWith(context.packageName + "/") }
        }
    }
}
