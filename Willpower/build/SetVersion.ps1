# Takes a path to a VS 2013 .sln file, and makes a copy of the solution (and dependent projects) as VS2015.
param(
	[Parameter()]
	[string]$version,
	
	[Parameter()]
	[string]$revision,

	[Parameter()]
	[string]$productName = "Willpower Engine",

	[Parameter()]
	[string]$companyName = "",

	[Parameter()]
	[string]$copyright = "2016 wtmrsh@gmail.com"
)

If ($version -eq "")
{
    Write-Host "No -version parameter given!"
    Exit
}

If ($revision -eq "")
{
    Write-Host "No -revision parameter given!"
    Exit
}

# Get Version.h contents
$versionFile = "..\\version\\Version.h"
$fileExists = Test-Path $versionFile
if ($fileExists -eq $False)
{
    Write-Host "$versionFile not found!"
    Exit
}

$versionContentsIn = (Get-Content $versionFile) 

Write-Host "$version"
Write-Host "$revision"
Write-Host "$productName"
Write-Host "$companyName"
Write-Host "$copyright"

# Get short hash
$revision = $revision.Substring(0, 7)

# versions use full stops, not commas
$versionValue = $version -replace "\.", ","

# Parse and replace
$versionContentsIn = $versionContentsIn -replace "VER_PRODUCTNAME_STR\s+(.*)", "VER_PRODUCTNAME_STR`t`t`t`"$productName`""
$versionContentsIn = $versionContentsIn -replace "VER_FILEVERSION\s+(.*)", "VER_FILEVERSION`t`t`t`t$versionValue"
$versionContentsIn = $versionContentsIn -replace "VER_FILEVERSION_STR\s+(.*)", "VER_FILEVERSION_STR`t`t`t`"$version ($revision)`""

$versionContentsIn = $versionContentsIn -replace "VER_PRODUCTVERSION\s+(.*)", "VER_PRODUCTVERSION`t`t`t$versionValue"
$versionContentsIn = $versionContentsIn -replace "VER_PRODUCTVERSION_STR\s+(.*)", "VER_PRODUCTVERSION_STR`t`t`"$version ($revision)`""

$versionContentsIn = $versionContentsIn -replace "VER_COMPANYNAME_STR\s+(.*)", "VER_COMPANYNAME_STR`t`t`t`"$companyName`""
$versionContentsIn = $versionContentsIn -replace "VER_LEGALCOPYRIGHT_STR\s+(.*)", "VER_LEGALCOPYRIGHT_STR`t`t`"$copyright`""

# Write contents back to Version.h
$versionContentsIn | Set-Content $versionFile
