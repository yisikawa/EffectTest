#======================================================================
#  RingStats.ps1 : --shot の PNG が「直線（中詰まり）」か「円周（中抜け）」
#                  かを数値で判定する
#
#  使い方:
#    powershell -ExecutionPolicy Bypass -File tools\RingStats.ps1 -Path out.png
#
#  出力（1 行）:
#    path=... bbox=WxH aspect=0.40 fill=0.12 hollow=1.35 peak=0.31 rows=90
#
#  bbox   : 非背景画素の外接矩形（幅 x 高さ）
#  aspect : bbox 幅 / 高さ（参照の柱は 0.40 前後）
#  fill   : 非背景画素 / bbox 面積（円周配置だと小さくなる）
#  hollow : 中央帯の平均輝度 / 外周帯の平均輝度
#           < 1.0 = 中央が暗い = 中抜け（円周・リング状）
#           > 1.0 = 中央が明るい = 中詰まり（直線・塊状）
#  peak   : 中央行の輝度ピーク位置（0=中心 1=端。リングなら >0.2）
#  rows   : 判定に使った行数
#
#  背景色は左上の画素。比較は RGB 合計（0..765）。
#======================================================================
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [int]$Step = 2,
    [int]$Tolerance = 3
)

Add-Type -AssemblyName System.Drawing

$full = (Resolve-Path -LiteralPath $Path).Path
$bmp  = [System.Drawing.Bitmap]::FromFile($full)

try
{
    $W = $bmp.Width
    $H = $bmp.Height
    $bg = $bmp.GetPixel(0, 0)

    # --- 外接矩形 ---
    $minX = $W; $maxX = -1; $minY = $H; $maxY = -1; $count = 0
    for ($y = 0; $y -lt $H; $y += $Step)
    {
        for ($x = 0; $x -lt $W; $x += $Step)
        {
            $c = $bmp.GetPixel($x, $y)
            $d = [Math]::Abs($c.R - $bg.R) + [Math]::Abs($c.G - $bg.G) + [Math]::Abs($c.B - $bg.B)
            if ($d -le $Tolerance) { continue }
            $count++
            if ($x -lt $minX) { $minX = $x }
            if ($x -gt $maxX) { $maxX = $x }
            if ($y -lt $minY) { $minY = $y }
            if ($y -gt $maxY) { $maxY = $y }
        }
    }

    if ($maxX -lt 0)
    {
        "path=$Path bbox=0x0 aspect=0 fill=0 hollow=0 peak=0 rows=0"
        return
    }

    $bw = $maxX - $minX + 1
    $bh = $maxY - $minY + 1
    $aspect = [Math]::Round($bw / [double]$bh, 3)
    $area = [Math]::Max(1, [int]($bw / $Step) * [int]($bh / $Step))
    $fill = [Math]::Round($count / [double]$area, 3)

    # --- 中央帯 vs 外周帯の輝度（リング判定）---
    #   bbox の高さ方向は中央 60% の行だけを使う（上下端は形状が崩れやすい）
    $y0 = [int]($minY + $bh * 0.2)
    $y1 = [int]($minY + $bh * 0.8)
    $cx = ($minX + $maxX) / 2.0

    $sumIn = 0.0; $nIn = 0; $sumOut = 0.0; $nOut = 0
    for ($y = $y0; $y -le $y1; $y += $Step)
    {
        for ($x = $minX; $x -le $maxX; $x += $Step)
        {
            $c = $bmp.GetPixel($x, $y)
            $v = $c.R + $c.G + $c.B
            $dx = [Math]::Abs($x - $cx) / [Math]::Max(1.0, $bw / 2.0)   # 0=中心 1=端
            if ($dx -lt 0.25) { $sumIn += $v; $nIn++ }
            elseif ($dx -ge 0.55 -and $dx -le 0.95) { $sumOut += $v; $nOut++ }
        }
    }
    $avgIn  = if ($nIn  -gt 0) { $sumIn  / $nIn  } else { 0 }
    $avgOut = if ($nOut -gt 0) { $sumOut / $nOut } else { 0 }
    $hollow = if ($avgOut -gt 0) { [Math]::Round($avgIn / $avgOut, 3) } else { 0 }

    # --- 中央行のピーク位置（0=中心 1=端）---
    $ym = [int](($minY + $maxY) / 2)
    $best = -1.0; $bestD = 0.0
    for ($x = $minX; $x -le $maxX; $x += $Step)
    {
        $c = $bmp.GetPixel($x, $ym)
        $v = $c.R + $c.G + $c.B
        if ($v -gt $best) { $best = $v; $bestD = [Math]::Abs($x - $cx) / [Math]::Max(1.0, $bw / 2.0) }
    }
    $peak = [Math]::Round($bestD, 3)

    $rows = [int](($y1 - $y0) / $Step) + 1
    "path=$Path bbox=${bw}x${bh} aspect=$aspect fill=$fill hollow=$hollow peak=$peak rows=$rows"
}
finally
{
    $bmp.Dispose()
}
