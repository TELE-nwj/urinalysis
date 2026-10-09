package com.niaio.mobile.ui.theme

import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp

/**
 * 尿量监测仪 V1 设计令牌（Material 3）。
 *
 * 色值全部来自冻结的 UI 合同 source/docs/mobile/ui-v1.md：
 * 白底、深色数值、蓝色主按钮，报警用「文字 + 颜色」共同表达。
 * 这里只做一件事：把原先散落在界面代码里的裸色值收拢成 Material 3 色彩角色，
 * 并补齐合同未规定、但界面必然需要的中性色阶。合同里的语义一个都没有改动，
 * 也没有新增合同之外的能力或文案。
 */
object NiaioPalette {

    /** 合同：蓝色主按钮 */
    val Blue = Color(0xFF1565C0)
    val BlueDeep = Color(0xFF0B3C6E)
    val BlueWash = Color(0xFFE3ECF8)

    /** 合同：深色数值 */
    val Ink = Color(0xFF142E46)

    /** 合同：次要文本 */
    val Slate = Color(0xFF526273)

    /** 合同：报警 A 正常 */
    val Ok = Color(0xFF246444)
    val OkWash = Color(0xFFE4F0E9)

    /** 合同：SIMULATED 标记与数据陈旧提示 */
    val Warn = Color(0xFF9C4B00)
    val WarnWash = Color(0xFFFBEDDD)

    /** 合同：报警 B–E */
    val Danger = Color(0xFFB3261E)
    val DangerWash = Color(0xFFFBE9E7)

    /** 合同：白底 */
    val Paper = Color(0xFFFFFFFF)

    /** 卡片与分区底：与白底区分，但不改变「白底」的整体观感 */
    val PaperSunken = Color(0xFFF4F7FA)
    val PaperSunkenStrong = Color(0xFFE9EFF5)

    /** 描边 */
    val Outline = Color(0xFFC7D3DF)
    val OutlineSoft = Color(0xFFDCE4EC)
}

/**
 * 界面语气。合同要求报警「文字 + 颜色共同表达」，因此颜色永远成对出现在
 * 一个语义对象里，不允许单独取色。
 */
enum class NiaioTone { Ok, Warn, Danger, Info, Neutral }

/** 语义色的取值。底色用于容器，内容色用于文字与图标，两者对比度均 ≥ 4.5:1。 */
data class NiaioToneColors(val content: Color, val container: Color)

val NiaioTone.colors: NiaioToneColors
    get() = when (this) {
        NiaioTone.Ok -> NiaioToneColors(NiaioPalette.Ok, NiaioPalette.OkWash)
        NiaioTone.Warn -> NiaioToneColors(NiaioPalette.Warn, NiaioPalette.WarnWash)
        NiaioTone.Danger -> NiaioToneColors(NiaioPalette.Danger, NiaioPalette.DangerWash)
        NiaioTone.Info -> NiaioToneColors(NiaioPalette.BlueDeep, NiaioPalette.BlueWash)
        NiaioTone.Neutral -> NiaioToneColors(NiaioPalette.Slate, NiaioPalette.PaperSunken)
    }

private val NiaioColorScheme = lightColorScheme(
    primary = NiaioPalette.Blue,
    onPrimary = Color.White,
    primaryContainer = NiaioPalette.BlueWash,
    onPrimaryContainer = NiaioPalette.BlueDeep,

    secondary = NiaioPalette.Slate,
    onSecondary = Color.White,
    secondaryContainer = NiaioPalette.PaperSunkenStrong,
    onSecondaryContainer = NiaioPalette.Ink,

    tertiary = NiaioPalette.Ok,
    onTertiary = Color.White,
    tertiaryContainer = NiaioPalette.OkWash,
    onTertiaryContainer = NiaioPalette.Ok,

    background = NiaioPalette.Paper,
    onBackground = NiaioPalette.Ink,

    surface = NiaioPalette.Paper,
    onSurface = NiaioPalette.Ink,
    surfaceVariant = NiaioPalette.PaperSunken,
    onSurfaceVariant = NiaioPalette.Slate,
    surfaceTint = NiaioPalette.Blue,

    outline = NiaioPalette.Outline,
    outlineVariant = NiaioPalette.OutlineSoft,

    error = NiaioPalette.Danger,
    onError = Color.White,
    errorContainer = NiaioPalette.DangerWash,
    onErrorContainer = NiaioPalette.Danger,

    scrim = Color(0x66000000),
    inverseSurface = NiaioPalette.Ink,
    inverseOnSurface = Color.White,
    inversePrimary = NiaioPalette.BlueWash
)

private val NiaioShapes = Shapes(
    extraSmall = RoundedCornerShape(6.dp),
    small = RoundedCornerShape(10.dp),
    medium = RoundedCornerShape(16.dp),
    large = RoundedCornerShape(20.dp),
    extraLarge = RoundedCornerShape(28.dp)
)

/**
 * 读数排版：累计尿量、流速、滴数、计时、时钟每秒都可能刷新，
 * 比例数字会让整行宽度不断跳动。所有读数统一走等宽数字（tnum），
 * 字号与字重仍然取自 Material 3 的语义角色，不手写尺寸。
 */
val TabularNumerals = TextStyle(fontFeatureSettings = "tnum")

@Composable
fun NiaioTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = NiaioColorScheme,
        shapes = NiaioShapes,
        content = content
    )
}
