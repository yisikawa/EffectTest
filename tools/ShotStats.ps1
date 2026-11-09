#======================================================================
#  ShotStats.ps1 : --shot が出力した PNG を 4px 間引きで検査する
#
#  使い方:
#    powershell -ExecutionPolicy Bypass -File tools\ShotStats.ps1 -Path out_fire.png
#
#  出力（1 行）:
#    path=... size=1280x720 step=4 sampled=57600 nonBg=8199 maxSum=765
#
#  nonBg  : 背景色（左上の画素）と RGB が閾値以上違う点の数
#  maxSum : サンプル中の最大 RGB 合計（0..765）
#======================================================================
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [int]$Step = 4,
    [int]$Tolerance = 3
)

Add-Type -AssemblyName System.Drawing

$full = (Resolve-Path -LiteralPath $Path).Path
$bmp  = [System.Drawing.Bitmap]::FromFile($full)
try
{
    $bg = $bmp.GetPixel(0, 0)

    $sampled = 0
    $nonBg   = 0
    $maxSum  = 0

    for ($y = 0; $y -lt $bmp.Height; $y += $Step)
    {
        for ($x = 0; $x -lt $bmp.Width; $x += $Step)
        {
            $c = $bmp.GetPixel($x, $y)
            $sampled++

            $d = [Math]::Abs($c.R - $bg.R) + [Math]::Abs($c.G - $bg.G) + [Math]::Abs($c.B - $bg.B)
            if ($d -gt $Tolerance) { $nonBg++ }

            $s = $c.R + $c.G + $c.B
            if ($s -gt $maxSum) { $maxSum = $s }
        }
    }

    "path={0} size={1}x{2} step={3} sampled={4} nonBg={5} maxSum={6} bg={7},{8},{9}" -f `
        $Path, $bmp.Width, $bmp.Height, $Step, $sampled, $nonBg, $maxSum, $bg.R, $bg.G, $bg.B
}
finally
{
    $bmp.Dispose()
}
