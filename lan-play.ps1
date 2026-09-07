<#
.SYNOPSIS
  Starts several copies of the game on this machine, each in the LAN lobby, ready to play each
  other.

.DESCRIPTION
  The game refuses a second copy of itself unless -multiInstance is on the command line, and even
  with it two copies could not reach the LAN lobby together: the lobby binds one UDP port with no
  SO_REUSEADDR, so the second copy came up with a socket error and an empty game list. Three
  switches fix that, and this script is the four of them tied together.

  Every copy gets its own address out of 127.0.0.0/8 (-lanip). Windows routes that whole range to
  loopback and delivers a broadcast sent from one of those addresses to the others, which is what
  carries the lobby's game announcements. Every copy gets its own name (-lanname), because both
  read the same LAN preferences file and a host denies a join whose name it already holds.
  -lanlobby opens the LAN screen at startup instead of the main menu, which is the four clicks
  every copy would otherwise start with.

  What to do once the windows are up: in the first, click Host, pick a map, wait for the others to
  appear in the slots, then Start. In the others, the hosted game shows up in the game list; click
  it to join. Faction and colour are per slot, the way they always are.

.EXAMPLE
  .\lan-play.ps1
    Two copies, "Player 1" at 127.0.0.1 and "Player 2" at 127.0.0.2.

.EXAMPLE
  .\lan-play.ps1 -Copies 4 -Windowed:$false
    Four copies, fullscreen. Alt-tab between them; note that alt-tabbing out of a fullscreen game
    loses the D3D device, which the engine recovers from but which is a poor way to run four.
#>
param(
	# how many copies to start
	[int] $Copies = 2,
	# the exe to run, relative to Run\
	[string] $Exe = "generals.exe",
	# names for the copies; the n-th copy takes the n-th name, and anything past the end of the
	# list gets "Player <n>"
	[string[]] $Names = @(),
	# windowed is the only way to see two of them at once
	[bool] $Windowed = $true,
	# window size for each copy; two 800x600 windows sit side by side on a 1080p desktop
	[int] $Width = 800,
	[int] $Height = 600,
	# lay the windows out left to right instead of leaving them stacked on top of each other
	[bool] $Tile = $true,
	# anything else to hand every copy, e.g. -ExtraArgs '-noaudio'
	[string[]] $ExtraArgs = @()
)

$ErrorActionPreference = "Stop"

# in the source tree the exe is in GeneralsMD\Run; in an installed game this script sits next to it
$runDir = Join-Path $PSScriptRoot "GeneralsMD\Run"
if (-not (Test-Path (Join-Path $runDir $Exe)))
{
	$runDir = $PSScriptRoot
}
$exePath = Join-Path $runDir $Exe
if (-not (Test-Path $exePath))
{
	throw "no $Exe in $runDir - build it first (build.bat Release), or run this from the game folder"
}

if ($Copies -lt 2 -or $Copies -gt 8)
{
	throw "-Copies is 2..8; one copy does not need any of this"
}

$started = @()
for ($i = 1; $i -le $Copies; $i++)
{
	$name = if ($i -le $Names.Count) { $Names[$i - 1] } else { "Player $i" }
	$ip = "127.0.0.$i"

	# -quickstart drops the logos and the intro movie, which is two minutes of nothing when the point
	# of starting the game is that another copy of it is waiting in the lobby
	$argList = @("-multiInstance", "-quickstart", "-lanlobby", "-lanip", $ip, "-lanname", "`"$name`"")
	if ($Windowed) { $argList += @("-win", "-xres", $Width, "-yres", $Height) }
	$argList += $ExtraArgs

	# each copy writes its own log, or they overwrite each other's answers
	$logPrefix = "lan{0}_" -f $i
	$argList += @("-logPrefix", $logPrefix)

	$p = Start-Process -FilePath $exePath -WorkingDirectory $runDir -ArgumentList $argList -PassThru
	$started += [pscustomobject]@{ Copy = $i; Name = $name; IP = $ip; PID = $p.Id; Log = $logPrefix + "DebugLogFile.txt" }
}

if ($Tile -and $Windowed)
{
	# The copies are born at the splash's own size and centred, so without this they sit exactly on
	# top of each other and only the last one started is visible. Move them once each window exists;
	# the game itself never moves the window after the render device has resized it.
	Add-Type @"
using System;
using System.Runtime.InteropServices;
public class LanPlayWin {
	[DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
}
"@
	Add-Type -AssemblyName System.Windows.Forms
	$desk = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea

	$deadline = (Get-Date).AddSeconds(60)
	$x = 0
	$y = 0
	foreach ($s in $started)
	{
		$proc = Get-Process -Id $s.PID
		while ($proc.MainWindowHandle -eq 0 -and (Get-Date) -lt $deadline)
		{
			Start-Sleep -Milliseconds 500
			$proc.Refresh()
		}
		if ($proc.MainWindowHandle -ne 0)
		{
			# 0x0001 is SWP_NOSIZE: the size is the render device's business, the position is ours
			[LanPlayWin]::SetWindowPos($proc.MainWindowHandle, [IntPtr]::Zero, $x, $y, 0, 0, 0x0001) | Out-Null
		}
		$x += $Width + 8
		if ($x + $Width -gt $desk.Width)
		{
			$x = 0
			$y += $Height + 40
		}
	}
}

$started | Format-Table -AutoSize

Write-Host ""
Write-Host "Host in one window, join from the others. Logs are in $runDir."
Write-Host "If a game list stays empty, grep its log for 'Transport::init' - a bind failure there"
Write-Host "means two copies took the same -lanip."
