package com.agenda3000

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.webkit.JsResult
import android.webkit.ValueCallback
import android.webkit.WebChromeClient
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.webkit.WebViewAssetLoader
import org.json.JSONObject

/** Ventana única: un WebView que carga assets/index.html y un puente nativo ("Android") para avisos, archivos y sincronización. */
class MainActivity : Activity() {
    lateinit var web: WebView
    private var chooser: ValueCallback<Array<Uri>>? = null

    companion object {
        @Volatile var foreground = false
        const val REQ_FILE = 11
        const val REQ_IMPORT = 12
        const val REQ_PERM = 13
    }

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        web = WebView(this)
        setContentView(web)
        val loader = WebViewAssetLoader.Builder()
            .addPathHandler("/assets/", WebViewAssetLoader.AssetsPathHandler(this))
            .build()
        web.settings.javaScriptEnabled = true
        web.settings.domStorageEnabled = true       // localStorage: aquí se guardan los datos
        web.settings.allowFileAccess = false
        web.webViewClient = object : WebViewClient() {
            override fun shouldInterceptRequest(view: WebView?, request: WebResourceRequest?): WebResourceResponse? =
                request?.let { loader.shouldInterceptRequest(it.url) }
        }
        web.webChromeClient = object : WebChromeClient() {
            // confirm() de JavaScript (borrar objetos y rutinas)
            override fun onJsConfirm(view: WebView?, url: String?, message: String?, result: JsResult?): Boolean {
                AlertDialog.Builder(this@MainActivity).setMessage(message)
                    .setPositiveButton("OK") { _, _ -> result?.confirm() }
                    .setNegativeButton("CANCELAR") { _, _ -> result?.cancel() }
                    .setOnCancelListener { result?.cancel() }
                    .show()
                return true
            }
            // <input type=file>: foto de un objeto
            override fun onShowFileChooser(
                webView: WebView?, filePathCallback: ValueCallback<Array<Uri>>?, fileChooserParams: FileChooserParams?
            ): Boolean {
                chooser?.onReceiveValue(null)
                chooser = filePathCallback
                return try {
                    startActivityForResult(fileChooserParams!!.createIntent(), REQ_FILE)
                    true
                } catch (e: Exception) {
                    chooser = null
                    false
                }
            }
        }
        web.addJavascriptInterface(Bridge(this), "Android")
        if (Build.VERSION.SDK_INT >= 33 &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), REQ_PERM)
        web.loadUrl("https://appassets.androidplatform.net/assets/index.html")
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        when (requestCode) {
            REQ_FILE -> {
                chooser?.onReceiveValue(WebChromeClient.FileChooserParams.parseResult(resultCode, data))
                chooser = null
            }
            REQ_IMPORT -> {
                val uri = data?.data
                if (resultCode == RESULT_OK && uri != null) Thread {
                    val txt = try {
                        contentResolver.openInputStream(uri)?.bufferedReader()?.use { it.readText() } ?: ""
                    } catch (e: Exception) { "" }
                    js("window.onImport&&window.onImport(" + JSONObject.quote(txt) + ")")
                }.start()
            }
        }
    }

    fun js(code: String) = runOnUiThread { web.evaluateJavascript(code, null) }

    override fun onResume() { super.onResume(); foreground = true }
    override fun onPause() { foreground = false; super.onPause() }
}
