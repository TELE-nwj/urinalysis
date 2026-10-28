package com.niaio.mobile.ui

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.vector.addPathNodes
import androidx.compose.ui.unit.dp

/**
 * 界面图标。
 *
 * 刻意不引入 material-icons-extended：那是约 2.5 MB 的图标库，
 * 而这个界面只需要 8 个。这里按 Material 图标同样的 24dp 画布、
 * 同一套实心几何自绘，风格与系统 Material 图标一致，又不改变 APK 体积。
 */
object NiaioIcons {

    /** 暂停测量 */
    val Pause: ImageVector by lazy { material("Pause", "M6 19h4V5H6v14zm8-14v14h4V5h-4z") }

    /** 复位测量 */
    val Restart: ImageVector by lazy {
        material("Restart", "M12 5V1L7 6l5 5V7c3.31 0 6 2.69 6 6s-2.69 6-6 6-6-2.69-6-6H4c0 4.42 3.58 8 8 8s8-3.58 8-8-3.58-8-8-8z")
    }

    /** 电量 */
    val Battery: ImageVector by lazy {
        material("Battery", "M15.67 4H14V2h-4v2H8.33C7.6 4 7 4.6 7 5.33v15.33C7 21.4 7.6 22 8.33 22h7.33c.74 0 1.34-.6 1.34-1.33V5.33C17 4.6 16.4 4 15.67 4z")
    }

    /** U 盘 / 缓存存储 */
    val Storage: ImageVector by lazy {
        material("Storage", "M2 20h20v-4H2v4zm2-3h2v2H4v-2zM2 4v4h20V4H2zm4 3H4V5h2v2zm-4 7h20v-4H2v4zm2-3h2v2H4v-2z")
    }

    /** 设备时钟 */
    val Clock: ImageVector by lazy {
        material("Clock", "M11.99 2C6.47 2 2 6.48 2 12s4.47 10 9.99 10C17.52 22 22 17.52 22 12S17.52 2 11.99 2zM12 20c-4.42 0-8-3.58-8-8s3.58-8 8-8 8 3.58 8 8-3.58 8-8 8zm.5-13H11v6l5.25 3.15.75-1.23-4.5-2.67z")
    }

    /** 实时流速 */
    val Flow: ImageVector by lazy {
        material("Flow", "M20.38 8.57l-1.23 1.85a8 8 0 0 1-.22 7.58H5.07A8 8 0 0 1 15.58 6.85l1.85-1.23A10 10 0 0 0 3.35 19a2 2 0 0 0 1.72 1h13.85a2 2 0 0 0 1.74-1 10 10 0 0 0-.27-10.44zm-9.79 6.84a2 2 0 0 0 2.83 0l5.66-8.49-8.49 5.66a2 2 0 0 0 0 2.83z")
    }

    /** 累计滴数 / 尿量 */
    val Drop: ImageVector by lazy {
        material("Drop", "M12 2.69l5.66 5.66a8 8 0 1 1-11.31 0z")
    }

    /** Wi-Fi 连接 */
    val Wifi: ImageVector by lazy {
        material("Wifi", "M1 9l2 2c4.97-4.97 13.03-4.97 18 0l2-2C16.93 2.93 7.08 2.93 1 9zm8 8l3 3 3-3a4.237 4.237 0 0 0-6 0zm-4-4l2 2a7.074 7.074 0 0 1 10 0l2-2C15.14 9.14 8.87 9.14 5 13z")
    }

    /** 已确认 / 正常 */
    val Check: ImageVector by lazy {
        material("Check", "M9 16.17L4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41z")
    }

    /** 需要注意 / 报警 */
    val Warning: ImageVector by lazy {
        material("Warning", "M1 21h22L12 2 1 21zm12-3h-2v-2h2v2zm0-4h-2v-4h2v4z")
    }

    /** 说明 */
    val Info: ImageVector by lazy {
        material("Info", "M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm1 15h-2v-6h2v6zm0-8h-2V7h2v2z")
    }

    /** 系统设置 */
    val Settings: ImageVector by lazy {
        material(
            "Settings",
            "M19.14 12.94a7.5 7.5 0 0 0 0-1.88l2.03-1.58a.5.5 0 0 0 .12-.63l-1.92-3.32a.5.5 0 0 0-.6-.22l-2.39.96a7.3 7.3 0 0 0-1.63-.94l-.36-2.54a.5.5 0 0 0-.5-.42h-3.84a.5.5 0 0 0-.5.42l-.36 2.54c-.58.24-1.13.55-1.63.94l-2.39-.96a.5.5 0 0 0-.6.22L2.7 8.85a.5.5 0 0 0 .12.63l2.03 1.58a7.5 7.5 0 0 0 0 1.88L2.82 14.5a.5.5 0 0 0-.12.63l1.92 3.32c.13.22.4.3.6.22l2.39-.96c.5.39 1.05.7 1.63.94l.36 2.54c.04.24.25.42.5.42h3.84c.25 0 .46-.18.5-.42l.36-2.54c.58-.24 1.13-.55 1.63-.94l2.39.96c.22.09.47 0 .6-.22l1.92-3.32a.5.5 0 0 0-.12-.63l-2.03-1.58zM12 15.6A3.6 3.6 0 1 1 12 8.4a3.6 3.6 0 0 1 0 7.2z"
        )
    }

    private fun material(name: String, pathData: String): ImageVector =
        ImageVector.Builder(
            name = name,
            defaultWidth = 24.dp,
            defaultHeight = 24.dp,
            viewportWidth = 24f,
            viewportHeight = 24f
        ).apply {
            addPath(pathData = addPathNodes(pathData), fill = SolidColor(Color.Black))
        }.build()
}
