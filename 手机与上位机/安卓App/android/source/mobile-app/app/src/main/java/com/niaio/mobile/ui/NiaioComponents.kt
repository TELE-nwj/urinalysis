package com.niaio.mobile.ui

import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Button
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.niaio.mobile.ui.theme.NiaioPalette
import com.niaio.mobile.ui.theme.NiaioTone
import com.niaio.mobile.ui.theme.TabularNumerals
import com.niaio.mobile.ui.theme.colors

/**
 * 界面基础件。
 *
 * 全部走 Material 3 的语义角色（surface / onSurfaceVariant / primaryContainer …），
 * 颜色不写死在这里；只有合同规定的四个语义色通过 NiaioTone 成对取用，
 * 以保证报警永远「文字 + 颜色」同时表达。
 */

/** 状态徽标：连接状态、测量状态、数据新鲜度都用它。 */
@Composable
fun ToneChip(
    text: String,
    tone: NiaioTone,
    modifier: Modifier = Modifier,
    icon: ImageVector? = null,
    dot: Boolean = true,
    pulse: Boolean = false
) {
    val toneColors = tone.colors
    Surface(
        modifier = modifier,
        shape = CircleShape,
        color = toneColors.container,
        contentColor = toneColors.content
    ) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            modifier = Modifier.padding(horizontal = 11.dp, vertical = 6.dp)
        ) {
            if (icon != null) {
                Icon(icon, contentDescription = null, modifier = Modifier.size(14.dp))
                Spacer(Modifier.width(6.dp))
            } else if (dot) {
                StatusDot(color = toneColors.content, pulse = pulse)
                Spacer(Modifier.width(7.dp))
            }
            Text(text = text, style = MaterialTheme.typography.labelMedium, fontWeight = FontWeight.SemiBold)
        }
    }
}

/** 状态点。只在真的需要「正在进行」时才跑动画，减少无谓的重组。 */
@Composable
private fun StatusDot(color: Color, pulse: Boolean) {
    val alpha = if (pulse) {
        val transition = rememberInfiniteTransition(label = "dot-pulse")
        val animated by transition.animateFloat(
            initialValue = 1f,
            targetValue = 0.32f,
            animationSpec = infiniteRepeatable(
                animation = tween(durationMillis = 950),
                repeatMode = RepeatMode.Reverse
            ),
            label = "dot-alpha"
        )
        animated
    } else {
        1f
    }
    Box(modifier = Modifier.size(7.dp).alpha(alpha).background(color, CircleShape))
}

/** 横幅：只在「不是实时真实数据」时出现——离线陈旧、模拟演示。 */
@Composable
fun ToneBanner(
    tone: NiaioTone,
    icon: ImageVector,
    text: String,
    modifier: Modifier = Modifier
) {
    val toneColors = tone.colors
    Surface(modifier = modifier.fillMaxWidth(), color = toneColors.container, contentColor = toneColors.content) {
        Column {
            Row(
                modifier = Modifier.padding(horizontal = 20.dp, vertical = 12.dp),
                verticalAlignment = Alignment.Top
            ) {
                Icon(icon, contentDescription = null, modifier = Modifier.size(17.dp).padding(top = 1.dp))
                Spacer(Modifier.width(10.dp))
                Text(text = text, style = MaterialTheme.typography.bodySmall, lineHeight = 20.sp)
            }
            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
        }
    }
}

/** 分组容器：浅底圆角，用于设备状态、设备信息这类次要内容，不喧宾夺主。 */
@Composable
fun InfoCard(
    modifier: Modifier = Modifier,
    header: String? = null,
    headerChip: @Composable (() -> Unit)? = null,
    footer: String? = null,
    content: @Composable ColumnScope.() -> Unit
) {
    Surface(
        modifier = modifier.fillMaxWidth().padding(top = 18.dp),
        shape = MaterialTheme.shapes.large,
        color = MaterialTheme.colorScheme.surfaceVariant
    ) {
        Column(Modifier.padding(16.dp)) {
            if (header != null) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        text = header,
                        style = MaterialTheme.typography.titleSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.weight(1f)
                    )
                    headerChip?.invoke()
                }
                Spacer(Modifier.height(10.dp))
            }
            content()
            if (footer != null) {
                Spacer(Modifier.height(9.dp))
                Text(
                    text = footer,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

/** 设备状态 / 设备信息的一行。图标给扫读一个锚点，数值右对齐。 */
@Composable
fun StatusRow(
    icon: ImageVector,
    label: String,
    value: String,
    showDivider: Boolean = true,
    valueTone: NiaioTone? = null
) {
    if (showDivider) {
        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
    }
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier.fillMaxWidth().padding(vertical = 10.dp)
    ) {
        Icon(
            icon,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.size(17.dp)
        )
        Spacer(Modifier.width(9.dp))
        Text(
            text = label,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.weight(1f)
        )
        Spacer(Modifier.width(12.dp))
        Text(
            text = value,
            style = MaterialTheme.typography.bodyMedium.merge(TabularNumerals),
            fontWeight = FontWeight.SemiBold,
            color = valueTone?.colors?.content ?: MaterialTheme.colorScheme.onSurface,
            textAlign = TextAlign.End,
            modifier = Modifier.weight(1.4f)
        )
    }
}

/** 纯键值行，用于连接页的设备信息。 */
@Composable
fun KeyValueRow(
    label: String,
    value: String,
    showDivider: Boolean = true,
    monospace: Boolean = false,
    valueTone: NiaioTone? = null
) {
    if (showDivider) {
        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
    }
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier.fillMaxWidth().padding(vertical = 10.dp)
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.weight(1f)
        )
        Spacer(Modifier.width(12.dp))
        Text(
            text = value,
            style = MaterialTheme.typography.bodyMedium.merge(TabularNumerals),
            fontFamily = if (monospace) FontFamily.Monospace else null,
            fontWeight = if (monospace) FontWeight.Normal else FontWeight.SemiBold,
            color = valueTone?.colors?.content ?: MaterialTheme.colorScheme.onSurface,
            textAlign = TextAlign.End,
            modifier = Modifier.weight(1.8f)
        )
    }
}

/** 指标：标签在上、读数在下，读数用等宽数字，刷新时不跳宽。 */
@Composable
fun MetricTile(
    label: String,
    value: String,
    unit: String?,
    modifier: Modifier = Modifier
) {
    Column(modifier = modifier.padding(vertical = 16.dp)) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Spacer(Modifier.height(6.dp))
        Row(verticalAlignment = Alignment.Bottom) {
            Text(
                text = value,
                style = MaterialTheme.typography.headlineMedium.merge(TabularNumerals),
                fontWeight = FontWeight.SemiBold,
                color = MaterialTheme.colorScheme.onSurface,
                modifier = Modifier.weight(1f, fill = false).alignByBaseline()
            )
            if (unit != null) {
                Spacer(Modifier.width(4.dp))
                Text(
                    text = unit,
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.alignByBaseline()
                )
            }
        }
    }
}

/** 报警行：合同要求「文字 + 颜色共同表达」，因此底色与文字同取一个语义色对。 */
@Composable
fun AlertBar(
    tone: NiaioTone,
    icon: ImageVector,
    text: String,
    note: String? = null,
    modifier: Modifier = Modifier
) {
    val toneColors = tone.colors
    Surface(
        modifier = modifier.fillMaxWidth().padding(top = 18.dp),
        shape = MaterialTheme.shapes.large,
        color = toneColors.container,
        contentColor = toneColors.content
    ) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp)
        ) {
            Icon(icon, contentDescription = null, modifier = Modifier.size(19.dp))
            Spacer(Modifier.width(11.dp))
            Text(text = text, style = MaterialTheme.typography.titleSmall, fontWeight = FontWeight.Bold,
                modifier = Modifier.weight(1f))
            if (note != null) {
                Spacer(Modifier.width(12.dp))
                Text(
                    text = note,
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.weight(1f),
                    textAlign = TextAlign.End
                )
            }
        }
    }
}

/** 主操作。52dp 触摸高度，宽于 Material 默认的 40dp。 */
@Composable
fun PrimaryAction(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    icon: ImageVector? = null
) {
    Button(
        onClick = onClick,
        enabled = enabled,
        shape = MaterialTheme.shapes.medium,
        modifier = modifier.fillMaxWidth().heightIn(min = 52.dp)
    ) {
        if (icon != null) {
            Icon(icon, contentDescription = null, modifier = Modifier.size(19.dp))
            Spacer(Modifier.width(9.dp))
        }
        Text(text, style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.SemiBold)
    }
}

/** 次操作。 */
@Composable
fun SecondaryAction(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    icon: ImageVector? = null
) {
    OutlinedButton(
        onClick = onClick,
        enabled = enabled,
        shape = MaterialTheme.shapes.medium,
        modifier = modifier.fillMaxWidth().heightIn(min = 52.dp)
    ) {
        if (icon != null) {
            Icon(icon, contentDescription = null, modifier = Modifier.size(18.dp))
            Spacer(Modifier.width(9.dp))
        }
        Text(text, style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.SemiBold)
    }
}

/** 文字操作：入口类动作，保持 48dp 触摸目标但不与主按钮抢注意力。 */
@Composable
fun QuietAction(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true
) {
    TextButton(
        onClick = onClick,
        enabled = enabled,
        modifier = modifier.heightIn(min = 48.dp)
    ) {
        Text(text, style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.SemiBold)
    }
}

/** 命令已受理、设备尚未确认时的进行指示。合同要求此阶段禁止重复点击。 */
@Composable
fun WaitingIndicator(text: String, modifier: Modifier = Modifier) {
    Column(modifier = modifier.fillMaxWidth().padding(top = 11.dp)) {
        Text(
            text = text,
            style = MaterialTheme.typography.bodyMedium,
            color = NiaioPalette.Warn
        )
        Spacer(Modifier.height(8.dp))
        LinearProgressIndicator(
            modifier = Modifier.fillMaxWidth().height(3.dp),
            color = MaterialTheme.colorScheme.primary,
            trackColor = MaterialTheme.colorScheme.surfaceVariant
        )
    }
}

/** 分组标题。 */
@Composable
fun SectionTitle(text: String, modifier: Modifier = Modifier) {
    Text(
        text = text,
        style = MaterialTheme.typography.titleSmall,
        fontWeight = FontWeight.SemiBold,
        color = MaterialTheme.colorScheme.onSurface,
        modifier = modifier
    )
}

/** 连接步骤。 */
@Composable
fun StepItem(index: Int, title: String, detail: String?, modifier: Modifier = Modifier) {
    Row(modifier = modifier.fillMaxWidth(), verticalAlignment = Alignment.Top) {
        Box(
            modifier = Modifier
                .size(24.dp)
                .background(MaterialTheme.colorScheme.primaryContainer, CircleShape),
            contentAlignment = Alignment.Center
        ) {
            Text(
                text = index.toString(),
                style = MaterialTheme.typography.labelMedium,
                fontWeight = FontWeight.Bold,
                color = MaterialTheme.colorScheme.onPrimaryContainer,
                textAlign = TextAlign.Center
            )
        }
        Spacer(Modifier.width(12.dp))
        Column(Modifier.weight(1f)) {
            Text(
                text = title,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurface
            )
            if (detail != null) {
                Spacer(Modifier.height(3.dp))
                Text(
                    text = detail,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

internal val ActionGap = 9.dp

@Composable
internal fun ActionColumn(content: @Composable ColumnScope.() -> Unit) {
    Column(
        verticalArrangement = Arrangement.spacedBy(ActionGap),
        modifier = Modifier.fillMaxWidth(),
        content = content
    )
}

/** 主读数：整屏唯一的最大字号，合同里累计尿量专用。 */
@Composable
fun ReadoutValue(value: String, unit: String, modifier: Modifier = Modifier) {
    Row(modifier = modifier, verticalAlignment = Alignment.Bottom) {
        Text(
            text = value,
            style = MaterialTheme.typography.displayLarge.merge(TabularNumerals),
            fontWeight = FontWeight.Bold,
            color = MaterialTheme.colorScheme.onSurface,
            modifier = Modifier.weight(1f, fill = false).alignByBaseline()
        )
        Spacer(Modifier.width(8.dp))
        Text(
            text = unit,
            style = MaterialTheme.typography.titleLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.alignByBaseline()
        )
    }
}

/** 主读数下方的时间域注释。 */
@Composable
fun ReadoutNote(text: String, modifier: Modifier = Modifier) {
    Text(
        text = text,
        style = MaterialTheme.typography.bodyMedium.merge(TabularNumerals),
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        modifier = modifier
    )
}

@Composable
internal fun DisabledHint(text: String) {
    Text(
        text = text,
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant
    )
}
