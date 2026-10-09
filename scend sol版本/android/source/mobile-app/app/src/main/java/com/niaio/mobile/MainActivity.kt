package com.niaio.mobile

import android.Manifest
import android.content.ClipData
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.mutableStateOf
import androidx.core.content.FileProvider
import com.niaio.mobile.records.RecordCsv
import com.niaio.mobile.runtime.MonitorRuntime
import com.niaio.mobile.ui.AppActions
import com.niaio.mobile.ui.AppScreen
import com.niaio.mobile.ui.theme.NiaioTheme
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.Executors

/** Activity owns system dialogs; the service owns device monitoring. */
class MainActivity : ComponentActivity() {
    private val showExport = mutableStateOf(false)
    private val exportWorker = Executors.newSingleThreadExecutor()
    private var pendingExportFile: File? = null
    private val notificationPermission = registerForActivityResult(ActivityResultContracts.RequestPermission()) {
        MonitorRuntime.connect(applicationContext)
    }
    private val saveFile = registerForActivityResult(ActivityResultContracts.CreateDocument("text/csv")) { uri ->
        val file = pendingExportFile
        pendingExportFile = null
        if (uri != null && file != null) writeCsv(uri, file)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        savedInstanceState?.getString("EXPORT_FILE")?.takeIf { it.matches(Regex("NIAIO-records-[0-9-]+\\.csv")) }
            ?.let { pendingExportFile = File(File(cacheDir, "record_exports"), it) }
        MonitorRuntime.initialize(applicationContext)
        openRecordFrom(intent)
        val actions = AppActions(
            onConnect = ::connectWithNotificationPermission,
            onDisconnect = { MonitorRuntime.disconnect(applicationContext) },
            onCommand = MonitorRuntime::command,
            onWifiSettings = { startActivity(Intent(Settings.ACTION_WIFI_SETTINGS)) },
            onNotificationSettings = {
                startActivity(Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS)
                    .putExtra(Settings.EXTRA_APP_PACKAGE, packageName))
            },
            onClearLog = MonitorRuntime::clearLog,
            onRename = MonitorRuntime::renameRecord,
            onDeleteRecord = { MonitorRuntime.deleteRecord(it) },
            onDeleteAlarm = MonitorRuntime::deleteAlarm,
            onClearAll = MonitorRuntime::clearAll,
            onExport = { showExport.value = true },
            onRecordOpened = { MonitorRuntime.selectRecord(null) },
        )
        setContent {
            NiaioTheme {
                AppScreen(
                    probe = MonitorRuntime.probe.value,
                    book = MonitorRuntime.book.value,
                    requestedRecordId = MonitorRuntime.selectedRecord.value,
                    actions = actions,
                    persistenceStatus = MonitorRuntime.persistenceStatus.value,
                    notificationStatus = MonitorRuntime.notificationStatus.value,
                )
                if (showExport.value) AlertDialog(
                    onDismissRequest = { showExport.value = false },
                    title = { Text("导出手机记录") },
                    text = { Text("导出 CSV 文件，包含检测记录、分钟数据和警报。可保存到手机或分享，不修改 U 盘。") },
                    dismissButton = {
                        TextButton(onClick = { showExport.value = false; prepareExport(share = true) }) { Text("分享文件") }
                    },
                    confirmButton = {
                        TextButton(onClick = { showExport.value = false; prepareExport(share = false) }) { Text("保存到手机") }
                    },
                )
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        openRecordFrom(intent)
    }

    private fun openRecordFrom(intent: Intent) {
        if (intent.hasExtra("RECORD_ID")) {
            val id = intent.getLongExtra("RECORD_ID", -1)
            if (id > 0) MonitorRuntime.selectRecord(id)
            intent.removeExtra("RECORD_ID")
        }
    }

    private fun connectWithNotificationPermission() {
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            notificationPermission.launch(Manifest.permission.POST_NOTIFICATIONS)
        } else MonitorRuntime.connect(applicationContext)
    }

    private fun exportName() = "NIAIO-records-" + SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(Date()) + ".csv"

    private fun prepareExport(share: Boolean) {
        val book = MonitorRuntime.book.value
        exportWorker.execute {
            try {
                val csv = "\uFEFF" + RecordCsv.export(book)
                val directory = File(cacheDir, "record_exports")
                check(directory.isDirectory || directory.mkdirs()) { "无法创建导出目录" }
                val file = File(directory, exportName())
                file.writeText(csv, Charsets.UTF_8)
                if (share) {
                    val uri = FileProvider.getUriForFile(this, "$packageName.files", file)
                    runOnUiThread {
                        if (!isDestroyed) {
                            val send = Intent(Intent.ACTION_SEND).setType("text/csv")
                                .putExtra(Intent.EXTRA_STREAM, uri)
                                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                            send.clipData = ClipData.newRawUri("检测记录", uri)
                            startActivity(Intent.createChooser(send, "分享检测记录"))
                        }
                    }
                } else runOnUiThread {
                    if (!isDestroyed) {
                        pendingExportFile = file
                        saveFile.launch(file.name)
                    }
                }
            } catch (e: Exception) {
                report("导出失败：" + (e.message ?: "无法生成文件"))
            }
        }
    }

    private fun writeCsv(uri: Uri, file: File) {
        exportWorker.execute {
            try {
                val output = contentResolver.openOutputStream(uri, "wt") ?: error("无法打开文件")
                output.use { target -> file.inputStream().use { it.copyTo(target) }; target.flush() }
                report("记录已保存到手机文件")
            } catch (e: Exception) { report("保存失败：" + (e.message ?: "无法写入文件")) }
        }
    }

    private fun report(message: String) = runOnUiThread {
        if (!isDestroyed) Toast.makeText(this, message, Toast.LENGTH_LONG).show()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        pendingExportFile?.let { outState.putString("EXPORT_FILE", it.name) }
        super.onSaveInstanceState(outState)
    }

    override fun onDestroy() {
        exportWorker.shutdown()
        super.onDestroy()
    }
}

