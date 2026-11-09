param(
    [Parameter(Mandatory = $true)][string]$Dat,
    [string]$Base = 'C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM\0\0.DAT'
)

$genOps = @(0x02, 0x03, 0x05, 0x0A, 0x0B, 0x1C, 0x4A, 0x53, 0x60)

function Get-Chunks([byte[]]$b) {
    $n = New-Object System.Collections.ArrayList; $t = New-Object System.Collections.ArrayList
    $o = New-Object System.Collections.ArrayList; $s = New-Object System.Collections.ArrayList
    $pos = 0
    while ($pos + 16 -le $b.Length) {
        $hdr = [uint32][BitConverter]::ToUInt32($b, $pos + 4)
        $next = [int](([uint64]$hdr -shr 3) -band 0x7FFFF0)
        if ($next -lt 16 -or ($pos + $next) -gt $b.Length) { break }
        $nm = -join ([byte[]]$b[$pos..($pos + 3)] | ForEach-Object { if ($_ -ge 32 -and $_ -le 126) { [char]$_ } else { '.' } })
        $null = $n.Add($nm); $null = $t.Add([int]($hdr -band 0x7F)); $null = $o.Add($pos); $null = $s.Add($next)
        $pos += $next
    }
    return [pscustomobject]@{ N = $n; T = $t; O = $o; S = $s }
}

function Get-Name4([byte[]]$b, [int]$off) {
    return -join ([byte[]]$b[$off..($off + 3)] | ForEach-Object { if ($_ -ge 32 -and $_ -le 126) { [char]$_ } else { '.' } })
}

$bytes = [IO.File]::ReadAllBytes($Dat)
$c = Get-Chunks $bytes
$main = -1
for ($i = 0; $i -lt $c.N.Count; $i++) { if ($c.T[$i] -eq 0x07 -and $c.N[$i] -eq 'main') { $main = $i; break } }
if ($main -lt 0) { for ($i = 0; $i -lt $c.N.Count; $i++) { if ($c.T[$i] -eq 0x07) { $main = $i; break } } }
if ($main -lt 0) { Write-Output "[res] ERROR no 0x07 chunk"; exit 2 }

# 命令列（+0x50 固定 / len==0 終端）
$refs = @{}; $insts = 0; $spawn = 0
$pos = $c.O[$main] + 0x50; $end = $c.O[$main] + $c.S[$main]
while ($pos + 8 -le $end -and $insts -lt 4096) {
    $op = [int]$bytes[$pos]; $len = [int]$bytes[$pos + 1] -band 0x1F
    if ($len -eq 0) { break }
    $insts++
    if ($genOps -contains $op) { $spawn++; $refs[(Get-Name4 $bytes ($pos + 8))] = 1 }
    $pos += $len * 4
}

# base の 0x20 / 0x05 索引
$base20 = @{}; $base05 = @{}
if (Test-Path $Base) {
    $bb = [IO.File]::ReadAllBytes($Base); $bc = Get-Chunks $bb
    for ($i = 0; $i -lt $bc.N.Count; $i++) {
        if ($bc.T[$i] -eq 0x20) { $base20[$bc.N[$i]] = 1 }
        if ($bc.T[$i] -eq 0x05) { $base05[$bc.N[$i]] = 1 }
    }
}

$gens = 0; $genUsed = 0; $texNames = @{}
for ($i = 0; $i -lt $c.N.Count; $i++) {
    if ($c.T[$i] -eq 0x05) { $gens++; if ($refs.ContainsKey($c.N[$i])) { $genUsed++ } }
    if ($c.T[$i] -eq 0x20) { $texNames[$c.N[$i]] = 1 }
}

$used = @(); $baseTex = @(); $unused = @()
$nTex = 0; $nOther = 0; $nEmpty = 0; $nBase = 0; $nKw = 0; $nUnres = 0
for ($i = 0; $i -lt $c.N.Count; $i++) {
    if ($c.T[$i] -ne 0x05) { continue }
    $off = $c.O[$i] + 0xAC
    if ($off + 4 -gt $bytes.Length) { continue }
    $link = Get-Name4 $bytes $off
    if ($refs.ContainsKey($c.N[$i])) {
        if ($texNames.ContainsKey($link)) { $nTex++; if ($used -notcontains $link) { $used += $link } }
        elseif ($base20.ContainsKey($link)) { $nBase++; if ($baseTex -notcontains $link) { $baseTex += $link } }
        elseif ($link -eq 'dist') { $nKw++ }
        elseif ($link -match '^\.+$') { $nEmpty++ }
        elseif ($base05.ContainsKey($link)) { $nOther++ }
        else { $nUnres++ }
    }
}
foreach ($k in $texNames.Keys) { if ($used -notcontains $k) { $unused += $k } }

$name = Split-Path $Dat -Leaf
Write-Output ("[res] dat={0} chunks={1} gen={2} tex={3}" -f $name, $c.N.Count, $gens, $texNames.Count)
Write-Output ("[res] prog=0x50 insts={0} spawn={1}" -f $insts, $spawn)
Write-Output ("[res] gens={0}/{1}" -f $genUsed, $gens)
Write-Output ("[res] tex={0}/{1} ({2})" -f $used.Count, $texNames.Count, ($used -join ','))
Write-Output ("[res] base_tex={0} ({1})" -f $baseTex.Count, ($baseTex -join ','))
Write-Output ("[res] unused={0} ({1})" -f $unused.Count, ($unused -join ','))
Write-Output ("[res] link=tex:{0} other:{1} empty:{2} base:{3} keyword:{4} unresolved:{5}" -f $nTex, $nOther, $nEmpty, $nBase, $nKw, $nUnres)
Write-Output ("[res] SUMMARY dat={0} prog=0x50 insts={1} gen={2}/{3} tex={4}/{5} base={6} unused={7}" -f $name, $insts, $genUsed, $gens, $used.Count, $texNames.Count, $baseTex.Count, $unused.Count)
