param([string]$Controller='192.168.100.227')
$ErrorActionPreference='Stop'
$base="http://$Controller"
foreach ($path in @('status','names','automation','automation/safety','power/history')) {
    $response=Invoke-WebRequest -UseBasicParsing "$base/api/$path" -TimeoutSec 5
    $doc=$response.Content | ConvertFrom-Json
    if ($path -eq 'status' -and ($doc.relays.Count -ne 6 -or $doc.dimmers.Count -ne 2)) { throw 'Unexpected snapshot' }
    "$path HTTP $($response.StatusCode), $($response.RawContentLength) bytes"
}
$ws=[System.Net.WebSockets.ClientWebSocket]::new()
$cts=[System.Threading.CancellationTokenSource]::new(8000)
try {
    [void]$ws.ConnectAsync([uri]"ws://$Controller/ws",$cts.Token).GetAwaiter().GetResult()
    $request=[Text.Encoding]::UTF8.GetBytes('{"type":"get_status"}')
    [void]$ws.SendAsync([ArraySegment[byte]]::new($request),[System.Net.WebSockets.WebSocketMessageType]::Text,$true,$cts.Token).GetAwaiter().GetResult()
    $buffer=[byte[]]::new(8192)
    $types=@()
    while ($types -notcontains 'full_status' -or $types -notcontains 'telemetry') {
        $received=$ws.ReceiveAsync([ArraySegment[byte]]::new($buffer),$cts.Token).GetAwaiter().GetResult()
        if (!$received.EndOfMessage) { throw 'Unexpected fragment' }
        $doc=[Text.Encoding]::UTF8.GetString($buffer,0,$received.Count) | ConvertFrom-Json
        if ($types -notcontains $doc.type) { $types+=$doc.type; "WebSocket $($doc.type), $($received.Count) bytes" }
    }
} finally { $ws.Dispose(); $cts.Dispose() }
