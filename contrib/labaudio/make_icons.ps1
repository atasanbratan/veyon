# Draws the 128x128 Lab Audio feature icons (original artwork, no external assets).
param([string]$OutDir)
Add-Type -AssemblyName System.Drawing

function New-Canvas {
    $bmp = New-Object System.Drawing.Bitmap 128, 128, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.Clear([System.Drawing.Color]::Transparent)
    return @($bmp, $g)
}

$blue = [System.Drawing.Color]::FromArgb(255, 31, 111, 191)
$orange = [System.Drawing.Color]::FromArgb(255, 230, 140, 20)
$dark = [System.Drawing.Color]::FromArgb(255, 40, 50, 64)

function Draw-Headset($g, $color) {
    $pen = New-Object System.Drawing.Pen $color, 10
    $g.DrawArc($pen, 22, 18, 84, 84, 180, 180)
    $brush = New-Object System.Drawing.SolidBrush $color
    $g.FillRectangle($brush, 14, 60, 22, 38)
    $g.FillRectangle($brush, 92, 60, 22, 38)
}

function Draw-Waves($g, $color, $x, $y) {
    $pen = New-Object System.Drawing.Pen $color, 7
    $g.DrawArc($pen, $x, $y, 30, 40, -60, 120)
    $g.DrawArc($pen, $x + 10, $y - 10, 44, 60, -60, 120)
}

# Group icon: headset.
$c = New-Canvas; Draw-Headset $c[1] $blue; $c[0].Save("$OutDir\labaudio.png"); $c[1].Dispose(); $c[0].Dispose()

# Broadcast: speaker with waves.
$c = New-Canvas; $g = $c[1]
$brush = New-Object System.Drawing.SolidBrush $blue
$g.FillRectangle($brush, 14, 46, 24, 36)
$pts = [System.Drawing.Point[]]@((New-Object System.Drawing.Point 38, 46), (New-Object System.Drawing.Point 66, 22), (New-Object System.Drawing.Point 66, 106), (New-Object System.Drawing.Point 38, 82))
$g.FillPolygon($brush, $pts)
Draw-Waves $g $orange 70 44
$c[0].Save("$OutDir\broadcast.png"); $g.Dispose(); $c[0].Dispose()

# Talk: two speech bubbles.
$c = New-Canvas; $g = $c[1]
$g.FillEllipse((New-Object System.Drawing.SolidBrush $blue), 8, 16, 72, 52)
$g.FillPolygon((New-Object System.Drawing.SolidBrush $blue), [System.Drawing.Point[]]@((New-Object System.Drawing.Point 22, 60), (New-Object System.Drawing.Point 16, 82), (New-Object System.Drawing.Point 40, 64)))
$g.FillEllipse((New-Object System.Drawing.SolidBrush $orange), 48, 58, 72, 52)
$g.FillPolygon((New-Object System.Drawing.SolidBrush $orange), [System.Drawing.Point[]]@((New-Object System.Drawing.Point 106, 102), (New-Object System.Drawing.Point 112, 122), (New-Object System.Drawing.Point 90, 106)))
$c[0].Save("$OutDir\talk.png"); $g.Dispose(); $c[0].Dispose()

# Listen: headset with incoming waves (teacher listens).
$c = New-Canvas; $g = $c[1]
Draw-Headset $g $dark
$pen = New-Object System.Drawing.Pen $orange, 7
$g.DrawArc($pen, 44, 54, 40, 40, 200, 140)
$g.DrawArc($pen, 54, 66, 20, 20, 200, 140)
$c[0].Save("$OutDir\listen.png"); $g.Dispose(); $c[0].Dispose()
