param(
    [string]$HostName = "127.0.0.1",
    [int]$Port = 8080,
    [string]$UserName = "thanh",
    [string]$Token = "secret",
    [string]$Message = "A",
    [int]$TimeoutMs = 3000
)

$ErrorActionPreference = "Stop"

$client = [System.Net.Sockets.TcpClient]::new()
$client.ReceiveTimeout = $TimeoutMs
$client.SendTimeout = $TimeoutMs

try {
    $client.Connect($HostName, $Port)
    $stream = $client.GetStream()
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    $reader = [System.IO.StreamReader]::new($stream, $utf8NoBom, $false, 1024, $true)
    $writer = [System.IO.StreamWriter]::new($stream, $utf8NoBom, 1024, $true)
    $writer.NewLine = "`n"
    $writer.AutoFlush = $true

    $hello = $reader.ReadLine()
    $writer.WriteLine("AUTH $UserName $Token")
    $auth = $reader.ReadLine()
    $writer.WriteLine($Message)
    $echo = $reader.ReadLine()
    $writer.WriteLine("WHO")
    $whoHeader = $reader.ReadLine()
    $whoLine = $reader.ReadLine()

    [pscustomobject]@{
        ConnectedTo = "$HostName`:$Port"
        Hello = $hello
        Auth = $auth
        Echo = $echo
        WhoHeader = $whoHeader
        WhoLine = $whoLine
    }

    if ($auth -ne "AUTH OK") {
        throw "AUTH failed: $auth"
    }

    if ($echo -ne "echo: $Message") {
        throw "Echo mismatch: $echo"
    }
}
finally {
    if ($writer) {
        $writer.Dispose()
    }
    if ($reader) {
        $reader.Dispose()
    }
    $client.Close()
}
