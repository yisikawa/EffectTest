#======================================================================
#  Verify-All.ps1 : ゴールデン / selftest / 決定論 / 異常系の自動照合
#
#  使い方:
#    tools\Verify-All.ps1
#    tools\Verify-All.ps1 -Exe <exe> -Rom <ROM パス> -OutDir <作業ディレクトリ>
#
#  終了コード: 0 = 全 PASS / 1 = FAIL あり / 2 = 前提不足（exe/ROM が無い）
#======================================================================
param(
    [string]$Exe    = "",
    [string]$Rom    = "",
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrEmpty($Exe))    { $Exe    = Join-Path $root "x64\Release\EffectTest.exe" }
if ([string]::IsNullOrEmpty($Rom))    { $Rom    = "C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM" }
if ([string]::IsNullOrEmpty($OutDir)) { $OutDir = Join-Path $env:TEMP "effecttest-verify" }

if (-not (Test-Path $Exe)) { Write-Output ("ERROR exe not found: " + $Exe); exit 2 }
if (-not (Test-Path $Rom)) { Write-Output ("ERROR ROM not found: " + $Rom); exit 2 }
if (-not (Test-Path (Join-Path $PSScriptRoot "ShotStats.ps1"))) { Write-Output "ERROR tools\ShotStats.ps1 not found"; exit 2 }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$script:fail = 0

function Check([string]$name, [bool]$ok, [string]$detail)
{
    if ($ok) { Write-Output ("PASS  {0,-22} {1}" -f $name, $detail) }
    else     { Write-Output ("FAIL  {0,-22} {1}" -f $name, $detail); $script:fail++ }
}

function Q([string]$s) { return ('"' + $s + '"') }

# exe を起動して終了コードを返す（引数は呼び出し側で Q 済みの文字列を渡す）
function RunExe([string[]]$argv)
{
    $p = Start-Process -FilePath $Exe -ArgumentList $argv -Wait -PassThru -NoNewWindow -WorkingDirectory $root
    return $p.ExitCode
}

# ShotStats.ps1 の出力から nonBg / maxSum を取り出す
function ShotStats([string]$png)
{
    $out = & (Join-Path $PSScriptRoot "ShotStats.ps1") -Path $png
    foreach ($line in $out)
    {
        if ($line -match "nonBg=(\d+)\s+maxSum=(\d+)") { return @([int]$Matches[1], [int]$Matches[2]) }
    }
    return @(-1, -1)
}

#----------------------------------------------------------------------
# 1) 描画ゴールデン（docs/GOLDEN.md §2 / --frames 90 / 既定）
#----------------------------------------------------------------------
$golden = [ordered]@{
    '11\17' = @(10343, 204)
    '11\18' = @(3948,  454)
    '11\22' = @(970,   303)
    '12\43' = @(4661,  263)
}
foreach ($k in $golden.Keys)
{
    $png = Join-Path $OutDir ("g_" + ($k -replace '\\', '_') + ".png")
    $dat = Join-Path $Rom ($k + ".DAT")
    $rc  = RunExe @("--shot", (Q $dat), (Q $png), "--frames", "90")
    $st  = ShotStats $png
    $exp = $golden[$k]
    Check ("golden " + $k) (($rc -eq 0) -and ($st[0] -eq $exp[0]) -and ($st[1] -eq $exp[1])) `
          ("exit=$rc nonBg=$($st[0]) maxSum=$($st[1]) expect=$($exp[0])/$($exp[1])")
}

#----------------------------------------------------------------------
# 2) Cure tgt0（入れ子 Schedule / --shot は --loop off = 終了時凍結）
#----------------------------------------------------------------------
$png = Join-Path $OutDir "tgt0.png"
$rc  = RunExe @("--shot", (Q (Join-Path $Rom "10\9.DAT")), (Q $png), "--frames", "260", "--sched", "tgt0")
$st  = ShotStats $png
Check "cure tgt0" (($rc -eq 0) -and ($st[0] -eq 6991) -and ($st[1] -eq 313)) `
      ("exit=$rc nonBg=$($st[0]) maxSum=$($st[1]) expect=6991/313")

#----------------------------------------------------------------------
# 3) --selftest（11\17）: keys 全解決 / texnames==texspecific / rebuild=ok
#----------------------------------------------------------------------
$rc = RunExe @("--selftest", (Q (Join-Path $Rom "11\17.DAT")))
Start-Sleep -Milliseconds 300
$sel = Join-Path $root "effecttest_selftest.txt"
if (-not (Test-Path $sel))
{
    Check "selftest" $false "effecttest_selftest.txt が無い"
}
else
{
    $txt = Get-Content -LiteralPath $sel -Raw -Encoding UTF8
    $keysOk = $false
    if ($txt -match "keys\s*=\s*(\d+) ops, (\d+) resolved, (\d+) unresolved")
    {
        $keysOk = ([int]$Matches[3] -eq 0) -and ([int]$Matches[1] -eq [int]$Matches[2])
    }
    $texOk = $false
    if ($txt -match "texnames\s*=\s*(\d+) texspecific\s*=\s*(\d+)")
    {
        $texOk = ([int]$Matches[1] -eq [int]$Matches[2])
    }
    $rebuildOk = ($txt -match "rebuild\s*=\s*ok")
    Check "selftest" (($rc -eq 0) -and $keysOk -and $texOk -and $rebuildOk) `
          ("exit=$rc keys=$keysOk texnames=$texOk rebuild=$rebuildOk")
}

#----------------------------------------------------------------------
# 4) 決定論（同一コマンド 2 回で PNG の SHA-256 が一致）
#----------------------------------------------------------------------
$a = Join-Path $OutDir "det_a.png"
$b = Join-Path $OutDir "det_b.png"
$dat = Q (Join-Path $Rom "11\17.DAT")
RunExe @("--shot", $dat, (Q $a), "--frames", "30") | Out-Null
RunExe @("--shot", $dat, (Q $b), "--frames", "30") | Out-Null
$ha = (Get-FileHash -LiteralPath $a -Algorithm SHA256).Hash
$hb = (Get-FileHash -LiteralPath $b -Algorithm SHA256).Hash
Check "determinism" ($ha -eq $hb) ($ha.Substring(0, 16))

#----------------------------------------------------------------------
# 5) CLI 契約（異常系の終了コード）
#----------------------------------------------------------------------
$rc = RunExe @("--shot", $dat, (Q (Join-Path $OutDir "none.png")), "--bogus", "1")
Check "unknown flag -> 2" ($rc -eq 2) ("exit=$rc")

$rc = RunExe @("--action", $dat, "zzzz")
Check "unknown action -> 4" ($rc -eq 4) ("exit=$rc")

$rc = RunExe @("--shot", (Q "C:\nope\no_such.DAT"), (Q (Join-Path $OutDir "none.png")))
Check "missing dat -> 3" ($rc -eq 3) ("exit=$rc")

#----------------------------------------------------------------------
Write-Output ""
if ($script:fail -eq 0) { Write-Output "ALL PASS"; exit 0 }
Write-Output ("FAILED: " + $script:fail + " check(s)")
exit 1
