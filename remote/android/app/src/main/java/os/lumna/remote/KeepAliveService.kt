package os.lumna.remote

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder

/**
 * Mantém o processo (e a conexão com o PC) vivo com o app fechado, para os avisos, a área de
 * transferência e os arquivos continuarem chegando. Mostra um aviso fixo e discreto.
 */
class KeepAliveService : Service() {
    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        Connect.init(this)
        val manager = getSystemService(NotificationManager::class.java)
        manager.createNotificationChannel(NotificationChannel(CHANNEL, "Conexão com o PC", NotificationManager.IMPORTANCE_LOW))
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE)
        val notification = Notification.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.ic_launcher)
            .setContentTitle("LUMNA Connect")
            .setContentText("Mantendo a conexão com o PC")
            .setContentIntent(open)
            .setOngoing(true)
            .build()
        if (Build.VERSION.SDK_INT >= 29) {
            startForeground(ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
        } else {
            startForeground(ID, notification)
        }
        Connect.keepAlive = true
        Connect.post { Connect.resume() }
        return START_STICKY
    }

    override fun onDestroy() {
        Connect.keepAlive = false
        super.onDestroy()
    }

    companion object {
        private const val CHANNEL = "conexao"
        private const val ID = 1

        fun start(context: Context) {
            context.startForegroundService(Intent(context, KeepAliveService::class.java))
        }

        fun stop(context: Context) {
            context.stopService(Intent(context, KeepAliveService::class.java))
        }
    }
}
