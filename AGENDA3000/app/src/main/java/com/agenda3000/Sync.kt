package com.agenda3000

import android.bluetooth.BluetoothAdapter
import java.io.BufferedReader
import java.io.InputStream
import java.io.InputStreamReader
import java.io.OutputStream
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.Socket
import java.util.UUID
import kotlin.math.abs

/**
 * Mismo protocolo que el programa de PC (agenda_core.hpp):
 *   "AGENDA1 <reloj_ms>" / líneas "E|..", "R|..", "T|..", "X|..", "K|..", "N|.." y fotos "I|.." / "END"   (cada lado envía y recibe a la vez)
 * La fusión (gana el cambio más reciente) la hace JavaScript al recibir el resultado.
 */
object Sync {
    data class Result(val ok: Boolean, val msg: String, val peer: String = "", val offset: Long = 0)

    private val SPP: UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")

    fun bluetooth(addr: String, text: String): Result = try {
        @Suppress("DEPRECATION")
        val adapter = BluetoothAdapter.getDefaultAdapter()
        if (adapter == null) Result(false, "ESTE MÓVIL NO TIENE BLUETOOTH")
        else {
            adapter.cancelDiscovery()
            val s = adapter.getRemoteDevice(addr).createRfcommSocketToServiceRecord(SPP)
            s.connect()
            s.use { exchange(it.inputStream, it.outputStream, text) { it.close() } }
        }
    } catch (e: SecurityException) {
        Result(false, "FALTA EL PERMISO DE BLUETOOTH")
    } catch (e: Exception) {
        Result(false, "BLUETOOTH: " + (e.message ?: "NO SE PUDO CONECTAR"))
    }

    /** "host:puerto" conecta; ":puerto" espera a que se conecte el otro equipo (90 s). */
    fun tcp(target: String, text: String): Result = try {
        val c = target.lastIndexOf(':')
        if (c < 0) Result(false, "FORMATO: IP:PUERTO O :PUERTO")
        else {
            val port = target.substring(c + 1).toIntOrNull() ?: 5050
            val sock: Socket =
                if (c == 0) {
                    val ss = ServerSocket()
                    ss.reuseAddress = true
                    ss.bind(InetSocketAddress(port))
                    ss.soTimeout = 90_000
                    ss.use { it.accept() }
                } else {
                    Socket().apply { connect(InetSocketAddress(target.substring(0, c), port), 10_000) }
                }
            sock.use { it.soTimeout = 20_000; exchange(it.getInputStream(), it.getOutputStream(), text) { it.close() } }
        }
    } catch (e: java.net.SocketTimeoutException) {
        Result(false, "NADIE SE CONECTÓ / SIN RESPUESTA")
    } catch (e: Exception) {
        Result(false, "RED: " + (e.message ?: "NO SE PUDO CONECTAR"))
    }

    private fun exchange(inp: InputStream, out: OutputStream, local: String, close: () -> Unit): Result {
        var writeError = false
        val writer = Thread {
            try {
                val body = if (local.isEmpty() || local.endsWith("\n")) local else local + "\n"
                out.write(("AGENDA1 " + System.currentTimeMillis() + "\n" + body + "END\n").toByteArray(Charsets.UTF_8))
                out.flush()
            } catch (e: Exception) { writeError = true }
        }
        writer.start()
        val watchdog = Thread {
            try { Thread.sleep(180_000); close() } catch (e: InterruptedException) { }
        }
        watchdog.isDaemon = true
        watchdog.start()

        val reader = BufferedReader(InputStreamReader(inp, Charsets.UTF_8))
        val sb = StringBuilder()
        var hello = false
        var end = false
        var peerNow = 0L
        var recvAt = 0L
        var count = 0
        try {
            while (true) {
                val line = reader.readLine() ?: break
                if (!hello) {
                    if (!line.startsWith("AGENDA1 ")) break
                    peerNow = line.substring(8).trim().toLongOrNull() ?: break
                    recvAt = System.currentTimeMillis()
                    hello = true
                } else if (line == "END") {
                    end = true
                    break
                } else if (line.length < 7000 && count < 400_000 &&
                    (line.startsWith("E|") || line.startsWith("R|") || line.startsWith("T|") ||
                     line.startsWith("X|") || line.startsWith("K|") || line.startsWith("N|") || line.startsWith("I|"))
                ) {
                    sb.append(line).append('\n')
                    count++
                }
            }
        } catch (e: Exception) { /* se informa abajo */ }
        watchdog.interrupt()
        writer.join(5_000)

        if (!hello) return Result(false, "SIN RESPUESTA DEL OTRO DISPOSITIVO")
        if (!end) return Result(false, "TRANSMISIÓN INCOMPLETA")
        if (writeError) return Result(false, "ERROR AL ENVIAR")
        var offset = peerNow - recvAt  // >0: el reloj del otro equipo va adelantado
        if (abs(offset) < 2000) offset = 0
        return Result(true, "", sb.toString(), offset)
    }
}
