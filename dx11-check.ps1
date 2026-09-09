# Photograph the same frame of the same match twice, once through Direct3D 9 and once through the
# Direct3D 11 backend, and count the pixels between them.
#
# This is RENDERER-ROADMAP.md phase 2's exit measurement.  Everything else about the backend is a
# count - buffers mirrored, pipelines built, draws made and refused - and a run can have all of
# those right and still draw the wrong picture: the white terrain had a correct atlas, a correct
# program and a correct draw count for two sessions.  Only two pictures of one frame settle it.
#
# All three shots come from one exe.  -dx11present is the only difference between them, so a view
# that comes back apart is the backend and not a build, a map or a seed.
#
# Three shots, not two.  The instrument has noise of its own: particles advance per rendered frame,
# and two runs of one unchanged binary come back up to 3.66% apart on Flash Effect.  A single D3D9
# against D3D11 pair therefore cannot say whether four percent is the backend or the smoke, so each
# view is photographed twice through Direct3D 9 as well and the pair's own disagreement is printed
# beside the signal.  A view passes when the backend is no further from Direct3D 9 than Direct3D 9
# is from itself, plus the margin.
#
# The views are tree-check.ps1's, for the same reason it has them: they are the eight positions in
# UI-MAP.md that between them frame terrain, trees, water, roads, shadows and the command bar.

# -Map narrows the run to the views whose map name contains it, for the middle of a hunt where one
# view answers the question and eight of them is twenty-four launches.  The table in
# RENDERER-ROADMAP.md is only ever written from a full run.
#
# -BackendNoise takes the repeated pair through the Direct3D 11 backend instead, which answers a
# different question: the printed floor is Direct3D 9 against itself, and a backend with a
# repeatability of its own would be measured against a floor that cannot see it.
#
# -Extra passes further switches to all three shots, which is how a pass is taken out of both sides
# at once: -Extra -nofx, -Extra -noshadowvolumes, -Extra -noshroud. A difference that collapses when
# one pass is gone belongs to that pass, and one that does not is somewhere else.
param([double]$Margin = 1.0, [string]$Map = '', [switch]$BackendNoise, [string[]]$Extra = @())

Add-Type -AssemblyName System.Drawing
$run = Join-Path $PSScriptRoot "GeneralsMD\Run"
$shots = "$env:USERPROFILE\Documents\Command and Conquer Generals Zero Hour Data"
$tmp = Join-Path $env:TEMP "dx11check"
if (-not (Test-Path $tmp)) { $null = New-Item -ItemType Directory $tmp }

$cases = @(
  @{map='Flash Effect';       x='1200'; y='945';  f=400},
  @{map='Flash Effect';       x='1816'; y='1861'; f=1200},
  @{map='Flash Effect';       x='1378'; y='1384'; f=2400},
  @{map='ForgottenForestZH';  x='1620'; y='1470'; f=600},
  @{map='ForgottenForestZH';  x='1543'; y='1636'; f=1800},
  @{map='Golden Oasis';       x='2643'; y='3580'; f=900},
  @{map='Alpine Assault';     x='760';  y='920';  f=700},
  @{map='Killing Fields';     x='1024'; y='1024'; f=1500}
)

# -msaa 0 for tree-check.ps1's reason: a multisampled back buffer cannot be read back, the capture
# falls through to a desktop grab, and whatever window is over the game lands in the .bmp.  The shot
# folder is emptied first so an empty one is a stated failure rather than the previous view's file.
function Shoot($c, $tag, $extra) {
  Get-ChildItem "$shots\sshot*.bmp" -ErrorAction SilentlyContinue | Remove-Item -Force
  $arguments = @('-win','-xres','1280','-yres','720','-quickstart','-noshellmap','-multiInstance',
    '-msaa','0','-map',"`"Maps\$($c.map)\$($c.map).map`"",'-autoskirmish','4','-aidiff','easy',
    '-seed','5','-maxframes',($c.f+80),'-screenshot',$c.f,'-camera',$c.x,$c.y,
    '-logPrefix',"dx11chk_$tag`_") + $extra + $Extra
  try {
    $process = Start-Process (Join-Path $run 'generals.exe') -ArgumentList $arguments -WorkingDirectory $run -PassThru
    $null = $process.WaitForExit(900000)
  }
  finally {
    Get-Process -Name generals -ErrorAction SilentlyContinue | Stop-Process -Force
  }
  $file = Get-ChildItem "$shots\sshot*.bmp" -ErrorAction SilentlyContinue |
          Sort-Object LastWriteTime | Select-Object -Last 1
  if ($null -eq $file) { throw "no screenshot for $tag" }
  $image = [System.Drawing.Image]::FromFile($file.FullName)
  $out = "$tmp\dx11chk_$tag.png"
  $image.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
  $image.Dispose()
  return $out
}

# Every other pixel of every other row, counting the ones more than 40 apart in summed channels.
#
# The whole frame, where tree-check.ps1 stops at row 520 to keep the command bar out of a comparison
# about trees.  Carrying that limit over here hid a bug for the length of the phase: the 2D layer was
# drawn half a pixel off under Direct3D 11, 45% of the command bar was past this threshold, and every
# number this script printed was blind to it.  An instrument that cannot see part of the picture
# cannot be the exit condition for drawing the picture.
#
# It returns the mean too, and the two say different things. The count is a coverage measure: a
# boundary pixel that one rasteriser fills and the other does not flips between a leaf and the sky
# and lands well past the threshold, so a frame that is aligned to the pixel and correct in every
# other way still counts a percent of its foliage edges. The mean is a fidelity measure: it is what
# the frame is wrong by, and a mean of a level a channel is the same picture whatever the count says.
# Quote both or neither.
function DiffPct($a, $b) {
  $ia = New-Object System.Drawing.Bitmap($a)
  $ib = New-Object System.Drawing.Bitmap($b)
  $w = $ia.Width; $h = $ia.Height
  $apart = 0; $total = 0; $sum = 0.0
  for ($y = 0; $y -lt $h; $y += 2) {
    for ($x = 0; $x -lt $w; $x += 2) {
      $pa = $ia.GetPixel($x, $y); $pb = $ib.GetPixel($x, $y)
      $d = [Math]::Abs($pa.R - $pb.R) + [Math]::Abs($pa.G - $pb.G) + [Math]::Abs($pa.B - $pb.B)
      $total++
      $sum += $d
      if ($d -gt 40) { $apart++ }
    }
  }
  $ia.Dispose(); $ib.Dispose()
  return @([Math]::Round(100.0 * $apart / $total, 2), [Math]::Round($sum / $total / 3.0, 2))
}

if ($Map -ne '') { $cases = @($cases | Where-Object { $_.map -like "*$Map*" }) }
if ($cases.Count -eq 0) { throw "no view matches -Map $Map" }

$fail = 0
foreach ($c in $cases) {
  $tag = ($c.map -replace '[^A-Za-z]','') + "_$($c.x)_$($c.f)"
  $nine = Shoot $c "d3d9a_$tag" @()
  $eleven = Shoot $c "dx11_$tag" @('-dx11present')
  if ($BackendNoise) {
    $again = Shoot $c "dx11b_$tag" @('-dx11present')
    $noise = DiffPct $eleven $again
  } else {
    $again = Shoot $c "d3d9b_$tag" @()
    $noise = DiffPct $nine $again
  }
  $signal = DiffPct $nine $eleven
  $verdict = if ($signal[0] -le ($noise[0] + $Margin)) { 'ok' } else { 'DIFFERENT' }
  if ($verdict -ne 'ok') { $fail++ }
  "{0,-20} cam {1,5},{2,-5} frame {3,-5} noise {4,5}%  dx11 {5,5}%  mean {6,5}  {7}" -f `
    $c.map, $c.x, $c.y, $c.f, $noise[0], $signal[0], $signal[1], $verdict
}
"---"
"pictures in $tmp"
if ($fail -eq 0) { "all $($cases.Count) views are within $Margin% of their own noise" }
else { "$fail of $($cases.Count) views are further than $Margin% past their own noise" }
exit $fail
