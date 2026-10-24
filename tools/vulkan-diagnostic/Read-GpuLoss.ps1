function Read-NewGpuLoss {
    param([string]$Path, [hashtable]$State)
    $needle = 'Device loss occurred!'
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    $stream = $null
    try {
        $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
            [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
        $length = $stream.Length
        if ($length -lt $State.Offset) { $State.Offset = 0L; $State.Tail = '' }
        if ($length -eq $State.Offset) { return $false }
        $start = [math]::Max([long]$State.Offset, $length - 65536L)
        if ($start -gt $State.Offset) { $State.Tail = '' }
        $stream.Position = $start
        $bytes = [byte[]]::new([int]($length - $start))
        $read = 0
        while ($read -lt $bytes.Length) {
            $next = $stream.Read($bytes, $read, $bytes.Length - $read)
            if ($next -eq 0) { break }
            $read += $next
        }
        $State.Offset = $start + $read
        $text = $State.Tail + [Text.Encoding]::UTF8.GetString($bytes, 0, $read)
        $State.Tail = $text.Substring([math]::Max(0, $text.Length - $needle.Length + 1))
        return $text.Contains($needle)
    } catch {
        return $false
    } finally {
        if ($null -ne $stream) { $stream.Dispose() }
    }
}
