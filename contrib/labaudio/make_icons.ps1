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

# Group work: three people (heads and shoulders), the middle one in the accent colour.
$c = New-Canvas; $g = $c[1]
function Draw-Person($g, $color, $x, $y, $scale) {
    $brush = New-Object System.Drawing.SolidBrush $color
    $g.FillEllipse($brush, $x - 14 * $scale, $y, 28 * $scale, 28 * $scale)
    $g.FillPie($brush, $x - 26 * $scale, $y + 30 * $scale, 52 * $scale, 52 * $scale, 180, 180)
}
Draw-Person $g $blue 30 34 0.9
Draw-Person $g $blue 98 34 0.9
Draw-Person $g $orange 64 22 1.1
$c[0].Save("$OutDir\group.png"); $g.Dispose(); $c[0].Dispose()

# Call the teacher: a raised hand on a round badge (blue = ready, orange = calling).
function Draw-Hand($g, $badge) {
    $g.FillEllipse((New-Object System.Drawing.SolidBrush $badge), 4, 4, 120, 120)
    $white = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::White)
    $g.FillRectangle($white, 38, 58, 52, 44)
    $g.FillEllipse($white, 38, 82, 52, 30)
    foreach ($f in @(@(38, 30), @(51, 22), @(64, 24), @(77, 32))) {
        $g.FillRectangle($white, $f[0], $f[1] + 6, 12, 50 - $f[1])
        $g.FillEllipse($white, $f[0], $f[1], 12, 12)
    }
    $thumb = New-Object System.Drawing.Pen ([System.Drawing.Color]::White), 13
    $thumb.StartCap = "Round"; $thumb.EndCap = "Round"
    $g.DrawLine($thumb, 44, 84, 27, 60)
}
$c = New-Canvas; Draw-Hand $c[1] $blue; $c[0].Save("$OutDir\hand.png"); $c[1].Dispose(); $c[0].Dispose()
$c = New-Canvas; Draw-Hand $c[1] $orange; $c[0].Save("$OutDir\hand-calling.png"); $c[1].Dispose(); $c[0].Dispose()
