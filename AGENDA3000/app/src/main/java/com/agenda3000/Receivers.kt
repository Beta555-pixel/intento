package com.agenda3000

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

class AlarmReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent?) = Alarms.fireDue(context)
}

/** Tras reiniciar el móvil o actualizar la app: recupera las alarmas pendientes. */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent?) = Alarms.fireDue(context)
}
