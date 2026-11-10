package com.agenda3000

import android.Manifest
import android.app.AlarmManager
import android.bluetooth.BluetoothAdapter
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.provider.Settings
import android.webkit.JavascriptInterface
import androidx.core.content.FileProvider
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/** Funciones nativas disponibles desde JavaScript como window.Android.* (se ejecutan en un hilo aparte). */
class Bridge(private val a: MainActivity) {

    /** json = [{"at":ms,"t":"título","b":"texto"}, ...] ordenado por hora; el sistema avisará aunque la app esté cerrada. */
    @JavascriptInterface fun scheduleAlarms(json: String) {
        Alarms.save(a, json)
        Alarms.scheduleNext(a)
    }

    /** Comparte agenda.txt (menú Compartir: Bluetooth, Nearby Share, Drive, USB...). */
    @JavascriptInterface fun exportText(text: String) {
        try {
            val f = File(a.cacheDir, "agenda.txt")
            f.writeText(text)
            val uri = FileProvider.getUriForFile(a, "com.agenda3000.files", f)
            val i = Intent(Intent.ACTION_SEND).apply {
                type = "text/plain"
                putExtra(Intent.EXTRA_STREAM, uri)
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
            a.runOnUiThread { a.startActivity(Intent.createChooser(i, "Enviar agenda")) }
        } catch (e: Exception) {
            a.js("toast('NO SE PUDO EXPORTAR')")
        }
    }

    /** Abre el selector de archivos; el contenido vuelve a JavaScript por window.onImport(texto). */
    @JavascriptInterface fun importFile() {
        a.runOnUiThread {
            val i = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                addCategory(Intent.CATEGORY_OPENABLE)
                type = "*/*"
            }
            a.startActivityForResult(i, MainActivity.REQ_IMPORT)
        }
    }

    @JavascriptInterface fun askPermissions() {
        a.runOnUiThread {
            val p = mutableListOf<String>()
            if (Build.VERSION.SDK_INT >= 33) p += Manifest.permission.POST_NOTIFICATIONS
            if (Build.VERSION.SDK_INT >= 31) p += Manifest.permission.BLUETOOTH_CONNECT
            if (p.isNotEmpty()) a.requestPermissions(p.toTypedArray(), MainActivity.REQ_PERM)
            if (Build.VERSION.SDK_INT >= 31) {
                val am = a.getSystemService(AlarmManager::class.java)
                if (!am.canScheduleExactAlarms())
                    a.startActivity(Intent(Settings.ACTION_REQUEST_SCHEDULE_EXACT_ALARM, Uri.parse("package:" + a.packageName)))
            }
        }
    }

    /** Dispositivos Bluetooth ya emparejados: [{"name":..., "addr":...}] o "NOPERM" si falta el permiso. */
    @JavascriptInterface fun bondedDevices(): String {
        if (Build.VERSION.SDK_INT >= 31 &&
            a.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED
        ) {
            a.runOnUiThread { a.requestPermissions(arrayOf(Manifest.permission.BLUETOOTH_CONNECT), MainActivity.REQ_PERM) }
            return "NOPERM"
        }
        val arr = JSONArray()
        try {
            BluetoothAdapter.getDefaultAdapter()?.bondedDevices?.forEach {
                arr.put(JSONObject().put("name", it.name ?: it.address).put("addr", it.address))
            }
        } catch (e: SecurityException) {
            return "NOPERM"
        }
        return arr.toString()
    }

    @JavascriptInterface fun syncBluetooth(addr: String, text: String) { Thread { report(Sync.bluetooth(addr, text)) }.start() }
    @JavascriptInterface fun syncTcp(target: String, text: String) { Thread { report(Sync.tcp(target, text)) }.start() }

    private fun report(r: Sync.Result) =
        a.js("window.onSync&&window.onSync(" + r.ok + "," + JSONObject.quote(r.msg) + "," + JSONObject.quote(r.peer) + "," + r.offset + ")")
}
