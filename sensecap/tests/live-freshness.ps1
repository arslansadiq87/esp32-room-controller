param([string]$Controller='192.168.100.227',[int]$Seconds=12)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Net.Http
$ws=[Net.WebSockets.ClientWebSocket]::new()
$http=[Net.Http.HttpClient]::new()
$limit=[Threading.CancellationTokenSource]::new(($Seconds+8)*1000)
try {
    [void]$ws.ConnectAsync([uri]"ws://$Controller/ws",$limit.Token).GetAwaiter().GetResult()
    $history=$http.GetAsync("http://$Controller/api/power/history")
    $clock=[Diagnostics.Stopwatch]::StartNew()
    $last=@{}; $count=@{}; $flags=@{}; $bad=0; $maxGap=0
    $bytes=[byte[]]::new(16384)
    while($clock.Elapsed.TotalSeconds -lt $Seconds) {
        $n=0
        do {
            $part=$ws.ReceiveAsync([ArraySegment[byte]]::new($bytes,$n,$bytes.Length-$n),$limit.Token).GetAwaiter().GetResult()
            if ($part.MessageType -eq 'Close') {throw 'WebSocket closed'}
            $n+=$part.Count
        } until ($part.EndOfMessage)
        $at=$clock.ElapsedMilliseconds
        try {$doc=[Text.Encoding]::UTF8.GetString($bytes,0,$n) | ConvertFrom-Json} catch {$bad++;continue}
        $categories=@()
        if ($doc.type -eq 'full_status') {$categories=@('snapshot','outputs','environment','radar','power')}
        if ($doc.type -eq 'telemetry') {
            if ($last.ContainsKey('telemetry')) { $maxGap=[Math]::Max([long]$maxGap,[long]($at-$last.telemetry)) }
            $categories=@('telemetry')
            foreach ($pair in @(@('environment','env_online'),@('radar','presence_online'),@('power','pwr_online'))) {
                $flag=$doc.PSObject.Properties[$pair[1]]
                if ($null -ne $flag) {
                    $categories+=$pair[0]
                    if ($flags[$pair[0]] -ne $flag.Value) { "t=$at ms $($pair[0]) online=$($flag.Value)" }
                    $flags[$pair[0]]=$flag.Value
                }
            }
        }
        if ($doc.type -in @('relay_update','dimmer_update')) {$categories=@('outputs')}
        foreach ($k in $categories){$count[$k]++;$last[$k]=$at}
    }
    foreach ($k in ($count.Keys | Sort-Object)) {"$k count=$($count[$k]) last_ms=$($last[$k]) age_ms=$($at-$last[$k])"}
    $response=$history.GetAwaiter().GetResult()
    "history HTTP $([int]$response.StatusCode), telemetry_max_gap_ms=$maxGap malformed=$bad"
    $response.Dispose()
} finally {$ws.Dispose();$http.Dispose();$limit.Dispose()}
