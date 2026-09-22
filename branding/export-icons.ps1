$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
function Export-Mark([string]$Destination, [int]$Size, [bool]$Foreground, [bool]$Light = $false, [bool]$Transparent = $false) {
    $bitmap = New-Object System.Drawing.Bitmap ($Size * 4), ($Size * 4)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    if (-not $Foreground -and -not $Transparent) { $graphics.Clear([System.Drawing.ColorTranslator]::FromHtml('#faf9f6')) }
    $scale = $Size * 4 / 96.0
    $graphics.ScaleTransform($scale, $scale)
    if ($Foreground) { $graphics.TranslateTransform(48,48); $graphics.ScaleTransform(0.70,0.70); $graphics.TranslateTransform(-48,-48) }
    $ink = if ($Light) { '#faf9f6' } else { '#202722' }
    $pen = New-Object System.Drawing.Pen ([System.Drawing.ColorTranslator]::FromHtml($ink)), 9
    $pen.StartCap = $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddBezier(48,18,66,18,76,28,76,43)
    $path.AddLine(76,43,76,48)
    $path.AddLine(76,48,58,48)
    foreach ($rotation in @(0,120,240)) {
        $state = $graphics.Save()
        $graphics.TranslateTransform(48,48)
        $graphics.RotateTransform($rotation)
        $graphics.TranslateTransform(-48,-48)
        $graphics.DrawPath($pen,$path)
        $graphics.Restore($state)
    }
    $small = New-Object System.Drawing.Bitmap $Size, $Size
    $resizer = [System.Drawing.Graphics]::FromImage($small)
    $resizer.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $resizer.DrawImage($bitmap,0,0,$Size,$Size)
    $small.Save($Destination,[System.Drawing.Imaging.ImageFormat]::Png)
    $resizer.Dispose(); $small.Dispose(); $path.Dispose(); $pen.Dispose(); $graphics.Dispose(); $bitmap.Dispose()
}
Export-Mark (Join-Path $PSScriptRoot 'synsemble.png') 512 $false $false $true
Export-Mark (Join-Path $PSScriptRoot 'synsemble-light.png') 512 $false $true $true
foreach ($density in @(@('mdpi',48,108),@('hdpi',72,162),@('xhdpi',96,216),@('xxhdpi',144,324),@('xxxhdpi',192,432))) {
    $directory = Join-Path $PSScriptRoot ('../android/res/mipmap-' + $density[0])
    Export-Mark (Join-Path $directory 'ic_launcher.png') $density[1] $false
    Export-Mark (Join-Path $directory 'ic_launcher_round.png') $density[1] $false
    Export-Mark (Join-Path $directory 'ic_launcher_foreground.png') $density[2] $true
}
