<#
.SYNOPSIS
  Plays a fixed-seed skirmish, then plays its own replay back, and compares the two.

.DESCRIPTION
  P6 of PATHFINDING-PLAN.md. Every movement change in that plan edits GameLogic, which is the half
  of the engine that has to arrive at the same answer on every machine in a network game and on
  every playback of a replay. A change that plays beautifully and desyncs costs more than the
  smoothness it buys, and the failure is invisible in a single-machine test: the game plays fine,
  and then somebody cannot join.

  A replay is the cheap version of a second machine. It replays the command stream and nothing
  else, so if the logic is deterministic the playback walks the same frames as the recording. This
  runs a match, keeps its replay, plays that replay back, and compares the CRC of every object in
  the world at the frame both runs stopped on - the HEADLESS CRC line, which any build writes at
  the end of an unattended run.

  A mismatch names the frame it was taken at, not the frame it started at, so it says a divergence
  happened rather than where. That is still the whole difference between shipping a desync and
  not: -DebugCRCFromFrame in an _INTERNAL build is what narrows it down afterwards.

.EXAMPLE
  .\replay-check.ps1
  .\replay-check.ps1 -Seeds 0,1,2,3 -MaxFrames 6000
    Four seeds instead of two, shorter matches. Each seed costs two runs.
#>
param(
	# the matches to check; each one is played twice, once live and once from its own replay
	[int[]] $Seeds = @(0, 1),
	# players in each match
	[int] $Players = 2,
	# easy | medium | brutal
	[string] $Difficulty = "brutal",
	# how long each match runs. Long enough to build and fight; a divergence in movement code needs
	# units on the move, and the first thousand frames are two workers walking to a supply dock
	[int] $MaxFrames = 12000,
	# playable cells a side of the generated map
	[int] $MapCells = 128,
	# where the game is
	[string] $RunDir = "$PSScriptRoot\GeneralsMD\Run",
	[string] $Exe = "generals.exe",
	# switches to add to both halves of every seed. A movement flag has to be on for the recording
	# and the playback alike: turning it on for one of them is a divergence the script would report
	# as a broken build. -ExtraArgs -crowd is the crowd model's determinism check
	[string[]] $ExtraArgs = @()
)

$ErrorActionPreference = "Stop"

$exePath = Join-Path $RunDir $Exe
if (-not (Test-Path $exePath)) { throw "no $Exe in $RunDir" }

# The game writes its replays here and always to the same name, so a run has to be moved aside
# before the next one overwrites it.
$replayDir = Join-Path $env:USERPROFILE "Documents\Command and Conquer Generals Zero Hour Data\Replays"
$lastReplay = Join-Path $replayDir "00000000.rep"

function Invoke-Run([string[]] $extra, [string] $prefix)
{
	# -noFPSLimit for the same reason as in ai-batch.ps1: headless does not need it, and a run that
	# has its window back must not be paced by the monitor. It cannot touch the logic, so the CRC
	# this script compares is unaffected either way.
	$args = @("-headless", "-quickstart", "-noshellmap", "-multiInstance", "-noFPSLimit",
						"-maxframes", $MaxFrames, "-logPrefix", $prefix) + $extra + $ExtraArgs
	$proc = Start-Process -FilePath $exePath -ArgumentList $args -WorkingDirectory $RunDir -PassThru
	$proc.WaitForExit()
	$log = Join-Path $RunDir "$($prefix)DebugLogFile.txt"
	if (-not (Test-Path $log)) { return $null }
	$crcLine = Select-String -Path $log -Pattern "HEADLESS CRC: (0x[0-9A-F]+) at frame (\d+)" | Select-Object -Last 1
	$resLine = Select-String -Path $log -Pattern "HEADLESS RESULT: (.+)$" | Select-Object -Last 1
	if ($null -eq $crcLine) { return $null }
	return [pscustomobject]@{
		CRC    = $crcLine.Matches[0].Groups[1].Value
		Frame  = [int] $crcLine.Matches[0].Groups[2].Value
		Result = if ($resLine) { $resLine.Matches[0].Groups[1].Value } else { "" }
	}
}

$failures = 0
foreach ($seed in $Seeds)
{
	Write-Host ("seed {0}: recording ... " -f $seed) -NoNewline

	if (Test-Path $lastReplay) { Remove-Item $lastReplay -Force }

	# -observer, so both sides are AI and the command stream is entirely the AI's own decisions
	$live = Invoke-Run @("-randommap", $seed, $Players, $MapCells,
											 "-autoskirmish", $Players, "-aidiff", $Difficulty,
											 "-seed", $seed, "-observer") "det$($seed)_live"
	if ($null -eq $live) { Write-Host "no result from the live run"; $failures++; continue }
	if (-not (Test-Path $lastReplay)) { Write-Host "the live run wrote no replay"; $failures++; continue }

	# out of the way of the next recording, and under a name -replay can be given
	$kept = Join-Path $replayDir ("determinism{0}.rep" -f $seed)
	Move-Item $lastReplay $kept -Force

	Write-Host ("frame {0}, CRC {1}; playing back ... " -f $live.Frame, $live.CRC) -NoNewline

	$back = Invoke-Run @("-replay", ("determinism{0}" -f $seed)) "det$($seed)_back"
	if ($null -eq $back) { Write-Host "no result from the playback"; $failures++; continue }

	if ($back.CRC -eq $live.CRC -and $back.Frame -eq $live.Frame)
	{
		Write-Host ("same at frame {0}" -f $back.Frame)
	}
	else
	{
		Write-Host ("DIVERGED: live {0} at frame {1}, playback {2} at frame {3}" -f
								$live.CRC, $live.Frame, $back.CRC, $back.Frame)
		$failures++
	}
}

Write-Host ""
if ($failures -eq 0)
{
	Write-Host ("{0} of {0} replays played back to the same world." -f $Seeds.Count)
}
else
{
	Write-Host ("{0} of {1} did not. The logic is not deterministic; do not ship it." -f $failures, $Seeds.Count)
}
exit $failures
