package com.agenda3000

import android.app.AlarmManager
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.os.Build
import org.json.JSONArray

/**
 * La app web calcula las próximas alarmas (14 días) y las guarda aquí. Solo hay UNA alarma del sistema pendiente (la más próxima):
 * al dispararse se muestran las vencidas y se programa la siguiente. También tras reiniciar el móvil.
 */
object Alarms {
    private const val CH = "agenda"

    fun save(c: Context, json: String) = c.getSharedPreferences("al", 0).edit().putString("list", json).apply()

    private fun list(c: Context): JSONArray =
        try { JSONArray(c.getSharedPreferences("al", 0).getString("list", "[]")) } catch (e: Exception) { JSONArray() }

    fun scheduleNext(c: Context) {
        val am = c.getSystemService(AlarmManager::class.java)
        val pi = PendingIntent.getBroadcast(
            c, 0, Intent(c, AlarmReceiver::class.java), PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        am.cancel(pi)
        val now = System.currentTimeMillis()
        val arr = list(c)
        for (i in 0 until arr.length()) {
            val at = arr.getJSONObject(i).getLong("at")
            if (at > now) {
                if (Build.VERSION.SDK_INT < 31 || am.canScheduleExactAlarms())
                    am.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, at, pi)
                else
                    am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, at, pi)
                return
            }
        }
    }

    fun fireDue(c: Context) {
        val sp = c.getSharedPreferences("al", 0)
        val now = System.currentTimeMillis()
        val from = maxOf(sp.getLong("last", now - 120_000), now - 3_600_000)
        val arr = list(c)
        var shown = 0
        for (i in 0 until arr.length()) {
            val o = arr.getJSONObject(i)
            val at = o.getLong("at")
            // con la app abierta ya avisa ella misma (banner, sonido y vibración)
            if (at > from && at <= now && shown < 6 && !MainActivity.foreground) {
                show(c, o.getString("t"), o.optString("b"), ((at / 60000) % 100000).toInt() * 31 + i)
                shown++
            }
        }
        sp.edit().putLong("last", now).apply()
        scheduleNext(c)
    }

    fun show(c: Context, title: String, body: String, id: Int) {
        val nm = c.getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(
            NotificationChannel(CH, "AGENDA 3000", NotificationManager.IMPORTANCE_HIGH).apply { enableVibration(true) }
        )
        val open = PendingIntent.getActivity(
            c, 0, Intent(c, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        val n = Notification.Builder(c, CH)
            .setSmallIcon(R.drawable.ic_stat)
            .setContentTitle(title)
            .setContentText(body)
            .setStyle(Notification.BigTextStyle().bigText(body))
            .setCategory(Notification.CATEGORY_ALARM)
            .setAutoCancel(true)
            .setContentIntent(open)
            .build()
        nm.notify(id, n)  // sin permiso de notificaciones simplemente no se muestra
    }
}
