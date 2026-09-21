[CmdletBinding()]
param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot "..\build"),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot "output"),
    [string]$OutputFile = "flash-usage.html"
)

$ErrorActionPreference = "Stop"

function Resolve-RequiredPath([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Description was not found at '$Path'. Build the firmware first with 'idf.py build'."
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Convert-SizeToBytes([string]$Value) {
    if ($Value -notmatch '^\s*(\d+(?:\.\d+)?)\s*([KMG]?B)?\s*$') {
        throw "Unsupported flash-size value '$Value'."
    }
    $number = [double]$Matches[1]
    switch ($Matches[2].ToUpperInvariant()) {
        "KB" { return [uint64]($number * 1KB) }
        "MB" { return [uint64]($number * 1MB) }
        "GB" { return [uint64]($number * 1GB) }
        default { return [uint64]$number }
    }
}

function Format-Hex([uint64]$Value) {
    return ('0x{0:X}' -f $Value)
}

function Get-PartitionSubtype([byte]$Type, [byte]$Subtype) {
    if ($Type -eq 0) {
        if ($Subtype -eq 0) { return "factory" }
        if ($Subtype -ge 0x10 -and $Subtype -le 0x1F) { return "ota_$($Subtype - 0x10)" }
        if ($Subtype -eq 0x20) { return "test" }
    }
    if ($Type -eq 1) {
        $names = @{
            0 = "ota"; 1 = "phy"; 2 = "nvs"; 3 = "coredump"; 4 = "nvs_keys"
            5 = "efuse"; 0x80 = "undefined"; 0x81 = "fat"; 0x82 = "spiffs"; 0x83 = "littlefs"
        }
        if ($names.ContainsKey([int]$Subtype)) { return $names[[int]$Subtype] }
    }
    return ('0x{0:X2}' -f $Subtype)
}

function Read-PartitionTable([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    $reader = New-Object System.IO.BinaryReader($stream)
    $partitions = @()
    try {
        while (($stream.Position + 32) -le $stream.Length) {
            $magic = $reader.ReadUInt16()
            if ($magic -ne 0x50AA) { break }
            $type = $reader.ReadByte()
            $subtype = $reader.ReadByte()
            $offset = $reader.ReadUInt32()
            $size = $reader.ReadUInt32()
            $label = [Text.Encoding]::ASCII.GetString($reader.ReadBytes(16)).Trim([char]0)
            $flags = $reader.ReadUInt32()
            $partitions += [pscustomobject]@{
                name = $label
                type = if ($type -eq 0) { "app" } elseif ($type -eq 1) { "data" } else { ('0x{0:X2}' -f $type) }
                subtype = Get-PartitionSubtype $type $subtype
                offset = [uint64]$offset
                size = [uint64]$size
                end = [uint64]$offset + [uint64]$size
                flags = [uint64]$flags
            }
        }
    }
    finally {
        $reader.Dispose()
        $stream.Dispose()
    }
    return $partitions
}

function Get-Origin([string]$Directory, [string]$ProjectPath, [string]$IdfPath) {
    $normal = $Directory.Replace('\', '/').ToLowerInvariant()
    $project = $ProjectPath.Replace('\', '/').ToLowerInvariant().TrimEnd('/')
    $idf = $IdfPath.Replace('\', '/').ToLowerInvariant().TrimEnd('/')
    if ($normal.StartsWith("$project/components/") -or $normal.StartsWith("$project/main") -or
            $normal.StartsWith("$project/boards/")) { return "Project" }
    if ($normal.Contains('/thirdparty/esp-brookesia/')) { return "ESP-Brookesia source" }
    if ($normal.StartsWith("$project/managed_components/")) { return "Managed component" }
    if ($normal.StartsWith("$idf/")) { return "ESP-IDF" }
    return "Other"
}

$buildRoot = Resolve-RequiredPath $BuildDirectory "Build directory"
$flashArgsPath = Resolve-RequiredPath (Join-Path $buildRoot "flasher_args.json") "Flash arguments"
$partitionPath = Resolve-RequiredPath (Join-Path $buildRoot "partition_table\partition-table.bin") "Partition table"
$componentCsvPath = Resolve-RequiredPath (Join-Path $buildRoot "size-components.csv") "Component size report"
$descriptionPath = Resolve-RequiredPath (Join-Path $buildRoot "project_description.json") "Project description"

$flashArgs = Get-Content -LiteralPath $flashArgsPath -Raw | ConvertFrom-Json
$description = Get-Content -LiteralPath $descriptionPath -Raw | ConvertFrom-Json
$partitions = @(Read-PartitionTable $partitionPath | Sort-Object offset)
$flashSize = Convert-SizeToBytes $flashArgs.flash_settings.flash_size
$littleFsSource = Join-Path ([string]$description.project_path) "littlefs"
$littleFsContentBytes = if (Test-Path -LiteralPath $littleFsSource) {
    [uint64]((Get-ChildItem -LiteralPath $littleFsSource -File -Recurse | Measure-Object -Property Length -Sum).Sum)
} else { 0 }

$flashFilesByOffset = @{}
$flashArgs.flash_files.psobject.Properties | ForEach-Object {
    $offset = [Convert]::ToUInt64($_.Name.Substring(2), 16)
    $filePath = Join-Path $buildRoot ([string]$_.Value)
    $length = if (Test-Path -LiteralPath $filePath) { (Get-Item -LiteralPath $filePath).Length } else { 0 }
    $flashFilesByOffset[$offset] = [pscustomobject]@{
        path = [string]$_.Value
        size = [uint64]$length
    }
}

$partitionRows = foreach ($partition in $partitions) {
    $image = $flashFilesByOffset[$partition.offset]
    $imageSize = if ($null -ne $image) { [uint64]$image.size } else { 0 }
    $isLittleFs = $partition.subtype -eq "littlefs" -and $partition.name -match "littlefs"
    $used = if ($isLittleFs -and $littleFsContentBytes -gt 0) { $littleFsContentBytes } else { $imageSize }
    [pscustomobject]@{
        name = $partition.name
        type = $partition.type
        subtype = $partition.subtype
        offset = $partition.offset
        offsetHex = Format-Hex $partition.offset
        size = $partition.size
        end = $partition.end
        image = if ($null -ne $image) { $image.path } else { "" }
        imageSize = $imageSize
        used = $used
        free = if ($used -le $partition.size) { $partition.size - $used } else { 0 }
        utilization = if ($partition.size -gt 0) { [math]::Round(100.0 * $used / $partition.size, 2) } else { 0 }
        overflow = $used -gt $partition.size
        usageKind = if ($isLittleFs -and $littleFsContentBytes -gt 0) { "source files; filesystem overhead excluded" } else { "binary image" }
    }
}

$componentInfo = @{}
$description.build_component_info.psobject.Properties | ForEach-Object {
    $entry = $_.Value
    if ($entry.file) {
        $componentInfo[[IO.Path]::GetFileName([string]$entry.file)] = [pscustomobject]@{
            name = $_.Name
            directory = [string]$entry.dir
            origin = Get-Origin ([string]$entry.dir) ([string]$description.project_path) ([string]$description.idf_path)
            sourceCount = @($entry.sources).Count
        }
    }
}

$componentSizeRows = Get-Content -LiteralPath $componentCsvPath | Select-Object -Skip 1 | ForEach-Object {
    # ESP-IDF's report has repeated section names (for example, multiple
    # '.bss' columns), which Windows PowerShell's Import-Csv rejects. Only the
    # first two, stable columns are needed for flash attribution.
    if ($_ -match '^"([^"]+)","(\d+)"') {
        [pscustomobject]@{ ArchiveFile = $Matches[1]; TotalSize = [uint64]$Matches[2] }
    }
}

$componentRows = $componentSizeRows |
    Group-Object ArchiveFile |
    ForEach-Object {
        $archive = $_.Name
        $size = [uint64](($_.Group | Measure-Object -Property TotalSize -Sum).Sum)
        $info = $componentInfo[$archive]
        [pscustomobject]@{
            archive = $archive
            component = if ($null -ne $info) { $info.name } else { $archive -replace '^lib', '' -replace '\.a$', '' }
            size = $size
            origin = if ($null -ne $info) { $info.origin } else { "Unknown" }
            directory = if ($null -ne $info) { $info.directory } else { "" }
            sourceCount = if ($null -ne $info) { $info.sourceCount } else { 0 }
        }
    } |
    Sort-Object size -Descending

$originRows = $componentRows | Group-Object origin | ForEach-Object {
    [pscustomobject]@{
        origin = $_.Name
        size = [uint64](($_.Group | Measure-Object -Property size -Sum).Sum)
        components = $_.Count
    }
} | Sort-Object size -Descending

$mappedEnd = if ($partitions.Count -gt 0) { [uint64](($partitions | Measure-Object -Property end -Maximum).Maximum) } else { 0 }
$payloadBytes = [uint64](($flashFilesByOffset.Values | Measure-Object -Property size -Sum).Sum)
$appPartition = $partitionRows | Where-Object type -eq "app" | Select-Object -First 1

$report = [ordered]@{
    generatedAt = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss K")
    project = [ordered]@{
        name = $description.project_name
        version = $description.project_version
        target = $description.target
        idfVersion = $description.git_revision
        buildDirectory = $buildRoot
    }
    summary = [ordered]@{
        flashSize = $flashSize
        mappedEnd = $mappedEnd
        payloadBytes = $payloadBytes
        appUsed = if ($null -ne $appPartition) { $appPartition.used } else { 0 }
        appSize = if ($null -ne $appPartition) { $appPartition.size } else { 0 }
        appFree = if ($null -ne $appPartition) { $appPartition.free } else { 0 }
    }
    partitions = @($partitionRows)
    flashFiles = @($flashArgs.flash_files.psobject.Properties | ForEach-Object {
        $offset = [Convert]::ToUInt64($_.Name.Substring(2), 16)
        $file = $flashFilesByOffset[$offset]
        [pscustomobject]@{ offset = $offset; offsetHex = $_.Name; path = $file.path; size = $file.size }
    } | Sort-Object offset)
    components = @($componentRows)
    origins = @($originRows)
}

$json = $report | ConvertTo-Json -Depth 8 -Compress
$json = $json.Replace('</script>', '<\/script>')

$html = @'
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 flash usage dashboard</title>
<style>
:root { color-scheme: dark; --bg:#0c111b; --panel:#151d2b; --line:#273349; --text:#e9f0fa; --muted:#96a5ba; --accent:#8bd450; --warn:#ffb44c; --bad:#ff6577; }
* { box-sizing:border-box } body { margin:0; background:radial-gradient(circle at 15% 0,#17263b 0,transparent 36%),var(--bg); color:var(--text); font:14px/1.45 Inter,Segoe UI,sans-serif }
main { max-width:1440px; margin:auto; padding:28px } h1 { margin:0; font-size:clamp(28px,4vw,48px); letter-spacing:-.04em } h2 { margin:0 0 16px; font-size:20px } .sub { color:var(--muted); margin:5px 0 24px }
.cards { display:grid; grid-template-columns:repeat(auto-fit,minmax(190px,1fr)); gap:12px; margin:20px 0 }.card,.panel { background:linear-gradient(145deg,#182235,#111925); border:1px solid var(--line); border-radius:15px; box-shadow:0 18px 45px #05081055 }
.card { padding:17px }.label { color:var(--muted); font-size:12px; text-transform:uppercase; letter-spacing:.11em }.value { margin-top:5px; font-size:25px; font-weight:700 }.panel { padding:20px; margin:14px 0; overflow:hidden }
.legend { display:flex; flex-wrap:wrap; gap:12px; margin:14px 0 0 }.legend span { display:inline-flex; align-items:center; gap:6px; color:var(--muted) }.swatch { width:10px; height:10px; border-radius:3px }
svg { display:block; width:100%; height:auto }.tip { position:fixed; display:none; pointer-events:none; padding:9px 11px; border:1px solid #41516d; border-radius:8px; background:#090e17f2; box-shadow:0 8px 30px #0008; white-space:pre; z-index:3 }
.grid { display:grid; grid-template-columns:minmax(0,1.15fr) minmax(330px,.85fr); gap:14px } table { width:100%; border-collapse:collapse } th,td { padding:9px 8px; border-bottom:1px solid var(--line); text-align:left } th { color:var(--muted); font-size:11px; letter-spacing:.08em; text-transform:uppercase } td.num,th.num { text-align:right; font-variant-numeric:tabular-nums }.danger { color:var(--bad); font-weight:700 }.warning { color:var(--warn); font-weight:700 }
.bar { height:8px; overflow:hidden; background:#273349; border-radius:8px }.bar>i { display:block; height:100%; border-radius:inherit; background:var(--accent) }.path { max-width:400px; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; color:var(--muted) }
details summary { cursor:pointer; color:var(--accent) } footer { color:var(--muted); margin-top:18px } @media(max-width:900px){.grid{grid-template-columns:1fr}main{padding:18px}.wide{overflow-x:auto}}
</style>
</head>
<body><main>
<h1>Flash usage dashboard</h1><p class="sub" id="subtitle"></p>
<section class="cards" id="cards"></section>
<section class="panel"><h2>16 MB flash map</h2><svg id="flash-map" viewBox="0 0 1200 220" role="img" aria-label="Proportional flash partition map"></svg><div class="legend" id="legend"></div></section>
<section class="grid">
  <div class="panel"><h2>Partition utilisation</h2><div class="wide"><table><thead><tr><th>Partition</th><th>Type</th><th class="num">Offset</th><th class="num">Used / capacity</th><th>Use</th></tr></thead><tbody id="partitions"></tbody></table></div></div>
  <div class="panel"><h2>Largest linked components</h2><svg id="components-chart" viewBox="0 0 680 520" role="img" aria-label="Largest linked component sizes"></svg></div>
</section>
<section class="panel"><h2>Component and source detail</h2><div class="wide"><table><thead><tr><th>Component</th><th>Origin</th><th class="num">Linked size</th><th class="num">Sources</th><th>Source directory</th></tr></thead><tbody id="components"></tbody></table></div></section>
<section class="panel"><h2>Flashed images</h2><div class="wide"><table><thead><tr><th>File</th><th class="num">Offset</th><th class="num">Size</th></tr></thead><tbody id="files"></tbody></table></div></section>
<footer id="footer"></footer><div class="tip" id="tip"></div>
<script id="report-data" type="application/json">__REPORT_JSON__</script>
<script>
const d=JSON.parse(document.getElementById('report-data').textContent), NS='http://www.w3.org/2000/svg';
const colours=['#8bd450','#44c7f4','#a98cff','#ffb44c','#ff6577','#4bd5ae','#f58dd2','#7f95ff'];
const fmt=n=>n>=1048576?(n/1048576).toFixed(2)+' MiB':n>=1024?(n/1024).toFixed(1)+' KiB':n+' B';
const pct=(n,t)=>t?100*n/t:0, esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const el=(name,attrs={})=>{const e=document.createElementNS(NS,name);for(const[k,v]of Object.entries(attrs))e.setAttribute(k,v);return e};
document.getElementById('subtitle').textContent=`${d.project.name} ${d.project.version} · ${d.project.target} · ESP-IDF ${d.project.idfVersion} · generated ${d.generatedAt}`;
const cards=[['Flash capacity',fmt(d.summary.flashSize)],['Flashed payload',fmt(d.summary.payloadBytes)],['Application',fmt(d.summary.appUsed)],['Application free',fmt(d.summary.appFree)],['App utilisation',pct(d.summary.appUsed,d.summary.appSize).toFixed(1)+'%']];
document.getElementById('cards').innerHTML=cards.map(x=>`<div class="card"><div class="label">${x[0]}</div><div class="value">${x[1]}</div></div>`).join('');
const svg=document.getElementById('flash-map'), x0=24, width=1152, y=50, h=82, tip=document.getElementById('tip'), partitionColour=new Map(d.partitions.map((p,i)=>[p.offset,colours[i%colours.length]]));
svg.append(el('rect',{x:x0,y,width,height:h,rx:10,fill:'#222e41'}));
let cursor=0, segments=[]; for(const p of d.partitions){if(p.offset>cursor)segments.push({name:'Unassigned',offset:cursor,size:p.offset-cursor,type:'gap'});segments.push(p);cursor=Math.max(cursor,p.end)}if(cursor<d.summary.flashSize)segments.push({name:'Unassigned',offset:cursor,size:d.summary.flashSize-cursor,type:'gap'});
segments.forEach(p=>{const x=x0+width*p.offset/d.summary.flashSize,w=Math.max(.5,width*p.size/d.summary.flashSize),c=p.type==='gap'?'#273349':partitionColour.get(p.offset),r=el('rect',{x,y,width:w,height:h,fill:c,'data-tip':`${p.name}\n${fmt(p.size)}\n0x${p.offset.toString(16).toUpperCase()}`});r.addEventListener('mousemove',showTip);r.addEventListener('mouseleave',hideTip);svg.append(r);if(w>62&&p.type!=='gap'){const t=el('text',{x:x+w/2,y:y+37,'text-anchor':'middle',fill:'#081019','font-size':13,'font-weight':700});t.textContent=p.name;svg.append(t)}});
for(let i=0;i<=8;i++){const x=x0+width*i/8,t=el('text',{x,y:165,'text-anchor':i===0?'start':i===8?'end':'middle',fill:'#96a5ba','font-size':12});t.textContent=fmt(d.summary.flashSize*i/8);svg.append(t);svg.append(el('line',{x1:x,x2:x,y1:137,y2:146,stroke:'#61718a'}))}
document.getElementById('legend').innerHTML=d.partitions.map((p,i)=>`<span><i class="swatch" style="background:${colours[i%colours.length]}"></i>${esc(p.name)}</span>`).join('');
document.getElementById('partitions').innerHTML=d.partitions.map(p=>{const pc=p.utilization,cl=p.overflow?'danger':pc>=90?'warning':'';return `<tr><td><strong>${esc(p.name)}</strong><div class="path">${esc(p.image||'No image mapped')}</div></td><td>${esc(p.type)} / ${esc(p.subtype)}</td><td class="num">${p.offsetHex}</td><td class="num ${cl}">${fmt(p.used)} / ${fmt(p.size)}<div class="path">${esc(p.usageKind)}</div></td><td><div class="bar"><i style="width:${Math.min(100,pc)}%;background:${p.overflow?'#ff6577':pc>=90?'#ffb44c':'#8bd450'}"></i></div><small>${pc.toFixed(1)}%</small></td></tr>`}).join('');
const topComponents=d.components.slice(0,15), chart=document.getElementById('components-chart'), maxComponentSize=Math.max(...topComponents.map(c=>c.size),1);topComponents.forEach((c,i)=>{const yy=12+i*33,w=390*c.size/maxComponentSize;const name=el('text',{x:8,y:yy+17,fill:'#dce7f7','font-size':12});name.textContent=c.component.slice(0,28);chart.append(name);chart.append(el('rect',{x:220,y:yy+3,width:w,height:20,rx:4,fill:colours[i%colours.length]}));const val=el('text',{x:225+w,y:yy+17,fill:'#dce7f7','font-size':11});val.textContent=fmt(c.size);chart.append(val)});
document.getElementById('components').innerHTML=d.components.map(c=>`<tr><td><strong>${esc(c.component)}</strong><div class="path">${esc(c.archive)}</div></td><td>${esc(c.origin)}</td><td class="num">${fmt(c.size)}</td><td class="num">${c.sourceCount}</td><td class="path" title="${esc(c.directory)}">${esc(c.directory)}</td></tr>`).join('');
document.getElementById('files').innerHTML=d.flashFiles.map(f=>`<tr><td>${esc(f.path)}</td><td class="num">${f.offsetHex}</td><td class="num">${fmt(f.size)}</td></tr>`).join('');
document.getElementById('footer').textContent=`Analysed ${d.components.length} linked archives. Source: ${d.project.buildDirectory}`;
function showTip(e){tip.textContent=e.target.dataset.tip;tip.style.display='block';tip.style.left=(e.clientX+14)+'px';tip.style.top=(e.clientY+14)+'px'}function hideTip(){tip.style.display='none'}
</script></main></body></html>
'@

$html = $html.Replace('__REPORT_JSON__', $json)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$outputPath = Join-Path $OutputDirectory $OutputFile
[IO.File]::WriteAllText($outputPath, $html, (New-Object Text.UTF8Encoding($false)))
Write-Host "Flash dashboard written to $outputPath"
