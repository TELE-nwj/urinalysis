package com.niaio.mobile.records

import android.content.Context
import android.util.AtomicFile
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileNotFoundException
import java.io.IOException

class RecordStorageException(message: String, cause: Throwable? = null) : IOException(message, cause)

/** App-private atomic storage. A failed read is explicit and disables overwriting that repository. */
class RecordRepository(context: Context) {
    private val file = AtomicFile(File(context.applicationContext.filesDir, "local-records-v1.bin"))
    private var verified = false
    private var readFailure: RecordStorageException? = null

    @Synchronized fun load(): RecordBookState {
        try {
            val bytes = file.openRead().use { input ->
                val output = ByteArrayOutputStream(); val buffer = ByteArray(8192)
                while (true) {
                    val count = input.read(buffer)
                    if (count < 0) break
                    if (output.size().toLong() + count > RecordCodec.MAX_BYTES) throw IOException("文件超过大小上限")
                    output.write(buffer, 0, count)
                }
                output.toByteArray()
            }
            val state = RecordCodec.decode(bytes)
            verified = true; readFailure = null
            return state
        } catch (missing: FileNotFoundException) {
            if (!file.baseFile.exists() && !File(file.baseFile.path + ".bak").exists() && !File(file.baseFile.path + ".new").exists()) {
                verified = true; readFailure = null
                return RecordBookState()
            }
            throw failRead(missing)
        } catch (error: IOException) { throw failRead(error) }
    }

    @Synchronized fun save(state: RecordBookState) {
        readFailure?.let { throw RecordStorageException("本地记录读取失败，已禁止覆盖原文件；请保留文件并处理恢复", it) }
        // A caller cannot bypass validation by saving before its first load.
        if (!verified) load()
        val bytes = try { RecordCodec.encode(state) }
            catch (error: IOException) { throw RecordStorageException("本地记录未保存：${error.message}", error) }
        var output: java.io.FileOutputStream? = null
        try {
            output = file.startWrite(); output.write(bytes); file.finishWrite(output)
        } catch (error: Exception) {
            output?.let { file.failWrite(it) }
            throw RecordStorageException("本地记录保存失败，原文件已保留", error)
        }
    }

    private fun failRead(cause: IOException): RecordStorageException {
        verified = false
        return RecordStorageException("本地记录读取失败，原文件未覆盖；需要恢复处理：${cause.message}", cause)
            .also { readFailure = it }
    }
}
