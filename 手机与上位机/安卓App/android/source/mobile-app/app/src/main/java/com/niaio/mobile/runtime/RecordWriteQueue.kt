package com.niaio.mobile.runtime

import com.niaio.mobile.records.RecordBookState
import com.niaio.mobile.records.RecordRepository
import java.util.concurrent.Executors

/** A single disk owner; keep only the newest unsaved immutable book, not a snapshot backlog. */
internal class RecordWriteQueue {
    private val worker = Executors.newSingleThreadExecutor { job -> Thread(job, "niaio-record-storage") }
    private val lock = Any()
    private var pending: Pair<Long, RecordBookState>? = null
    private var draining = false

    fun load(repository: RecordRepository, completed: (Result<RecordBookState>) -> Unit) {
        worker.execute { completed(runCatching { repository.load() }) }
    }

    fun save(repository: RecordRepository, revision: Long, state: RecordBookState,
             completed: (Long, Result<Unit>) -> Unit) {
        synchronized(lock) {
            pending = revision to state
            if (draining) return
            draining = true
            worker.execute {
                while (true) {
                    val item = synchronized(lock) {
                        val next = pending
                        pending = null
                        if (next == null) draining = false
                        next
                    } ?: break
                    completed(item.first, runCatching { repository.save(item.second) })
                }
            }
        }
    }

    /** Queued after the drain; completion means every book queued before this barrier was attempted. */
    fun flush(completed: () -> Unit) { worker.execute(completed) }
}
