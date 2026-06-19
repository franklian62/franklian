param(
    [string]$Port = "COM5",
    [int]$BaudRate = 115200
)

$ErrorActionPreference = "Stop"

$serial = [System.IO.Ports.SerialPort]::new($Port, $BaudRate, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$serial.NewLine = "`r`n"
$serial.ReadTimeout = 100
$serial.WriteTimeout = 100

try {
    $serial.Open()
    Write-Host "Serial monitor opened on $Port @ $BaudRate. Press Ctrl+C to stop."

    while ($true) {
        try {
            $data = $serial.ReadExisting()
            if ($data.Length -gt 0) {
                Write-Host -NoNewline $data
            }
        } catch [System.TimeoutException] {
        }

        Start-Sleep -Milliseconds 50
    }
} finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
}
