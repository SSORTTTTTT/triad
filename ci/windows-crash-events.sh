#!/bin/bash
# Prints Windows' own reports of programs that crashed in the last 4 hours (Application Error / Windows Error
# Reporting events: the program, the module the crash was in, the exception code and offset), or that would not start.
powershell -NoProfile -Command '
$since = (Get-Date).AddMinutes(-240)
$events = Get-WinEvent -FilterHashtable @{ LogName = "Application"; StartTime = $since } -ErrorAction SilentlyContinue |
          Where-Object { $_.ProviderName -in @("Application Error", "Windows Error Reporting", "SideBySide", "Application Hang") } |
          Select-Object -First 30
if (-not $events) { "No crash reports from Windows in the last 4 hours." }
foreach ($e in $events) { "---- " + $e.TimeCreated + " " + $e.ProviderName + " (" + $e.Id + ")"; $e.Message }
'
