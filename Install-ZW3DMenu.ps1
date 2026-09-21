$ErrorActionPreference = 'Stop'

$sourceSettings = Join-Path $PSScriptRoot 'Settings'
$userApiRoot = Join-Path $env:APPDATA 'ZWSOFT\ZW3D\ZW3D WuKong 2027\custom\apilibs'
$moduleRoot = Join-Path $userApiRoot 'MyFirstPluginUI'
$moduleSettings = Join-Path $moduleRoot 'Settings'
$pluginDll = Join-Path $userApiRoot 'MyFirstPlugin.dll'
$pluginResource = Join-Path $userApiRoot 'MyFirstPlugin.zrc'
$profileRoot = Join-Path $env:APPDATA 'ZWSOFT\ZW3D\ZW3D WuKong 2027\custom\profiles'
$profileResourcePool = Join-Path $profileRoot 'ResourcePool'
$sourceAction = Join-Path $sourceSettings 'Default\ResourcePool\MyFirstPlugin.zcui'
$sourceStrategyRoot = Join-Path $sourceSettings 'Default\Strategy\MyFirstPluginUI'

# Remove the unregistered layout used by the earlier implementation. These
# paths belong only to MyFirstPlugin and can otherwise shadow the new module.
$legacyDefaults = @(
    (Join-Path $userApiRoot 'Settings\Default'),
    'C:\Program Files\ZWSOFT\ZW3D WuKong 2027\apilibs\Settings\Default'
)

$legacyRelativeFiles = @(
    'Action\MyFirstPlugin.zcui',
    'Environment-2\Controls\MyFirstPlugin.zcui',
    'Environment-10\Controls\MyFirstPlugin.zcui',
    'Environment-13\Controls\MyFirstPlugin.zcui',
    'ResourcePool\MyFirstPlugin.zcui',
    'ResourcePool\RibbonPagesUser.zcui',
    'ResourcePool\MyFirstPluginRibbonPages.zcui'
)

$legacyRelativeDirectories = @(
    'Strategy\MyFirstPlugin-Z3',
    'Strategy\MyFirstPlugin-Part',
    'Strategy\MyFirstPlugin-Assembly',
    'Strategy\Environment-2-Z3',
    'Strategy\Environment-10-Part',
    'Strategy\Environment-13-Assembly',
    'Strategy\MyFirstPluginUI'
)

if (-not (Test-Path -LiteralPath $sourceSettings)) {
    throw "Menu resource directory not found: $sourceSettings"
}

foreach ($legacyDefault in $legacyDefaults) {
    foreach ($relativeFile in $legacyRelativeFiles) {
        $legacyFile = Join-Path $legacyDefault $relativeFile
        if (Test-Path -LiteralPath $legacyFile) {
            Remove-Item -LiteralPath $legacyFile -Force
        }
    }

    foreach ($relativeDirectory in $legacyRelativeDirectories) {
        $legacyDirectory = Join-Path $legacyDefault $relativeDirectory
        if (Test-Path -LiteralPath $legacyDirectory) {
            Remove-Item -LiteralPath $legacyDirectory -Recurse -Force
        }
    }
}

# Install into the current user's profile layer.  Unlike a profile module
# registered from the DLL Init function, these files are available before the
# ribbon is constructed and therefore do not depend on DLL load timing.
if (Test-Path -LiteralPath $moduleSettings) {
    Remove-Item -LiteralPath $moduleSettings -Recurse -Force
}

New-Item -ItemType Directory -Path $profileResourcePool -Force | Out-Null
$legacyProfileActions = @(
    (Join-Path $profileResourcePool 'MyFirstPlugin.zcui'),
    (Join-Path $profileResourcePool 'Actions.zcui')
)
foreach ($legacyProfileAction in $legacyProfileActions) {
    if (Test-Path -LiteralPath $legacyProfileAction) {
        $legacyContent = [System.IO.File]::ReadAllText(
            $legacyProfileAction,
            [System.Text.UTF8Encoding]::new($false)
        )
        if ($legacyContent.Contains('MyFirstPlugin') -or
            $legacyContent.Contains('MyOpenDialog')) {
            Remove-Item -LiteralPath $legacyProfileAction -Force
        }
    }
}
Copy-Item -LiteralPath $sourceAction -Destination (Join-Path $profileResourcePool 'MyFirstPluginActions.zcui') -Force

$profileStrategies = Get-ChildItem -LiteralPath $sourceStrategyRoot -File -Filter '*.zcui'
foreach ($strategy in $profileStrategies) {
    if ($strategy.BaseName -notmatch '^(Environment-.+)-(1-Primary|2-Intermediate_default|3-Advanced|4-Expert)$') {
        Write-Warning "Skipping unrecognized strategy filename: $($strategy.Name)"
        continue
    }

    $environmentName = $Matches[1]
    $roleName = $Matches[2]
    $environmentRoot = Join-Path (Join-Path $profileRoot $roleName) $environmentName
    New-Item -ItemType Directory -Path $environmentRoot -Force | Out-Null

    $profileStrategy = [System.IO.File]::ReadAllText(
        $strategy.FullName,
        [System.Text.UTF8Encoding]::new($false)
    )
    $profileStrategy = $profileStrategy.Replace('<DefaultCustomizations>', '<UserCustomizations>')
    $profileStrategy = $profileStrategy.Replace('</DefaultCustomizations>', '</UserCustomizations>')
    $targetStrategy = Join-Path $environmentRoot 'MyFirstPlugin.zcui'
    [System.IO.File]::WriteAllText(
        $targetStrategy,
        $profileStrategy,
        [System.Text.UTF8Encoding]::new($false)
    )
}

if (-not (Test-Path -LiteralPath $pluginDll)) {
    throw "Plugin DLL not found: $pluginDll. Build Debug | x64 first."
}

# Explicit registration makes startup loading deterministic.  ZW3D sometimes
# defers scanning the user apilibs directory until the Application Manager has
# been opened, which leaves the ribbon action unavailable during that session.
$zw3dRoot = 'HKCU:\Software\ZWSOFT\ZW3D'
$versionKey = Get-ChildItem -LiteralPath $zw3dRoot -ErrorAction Stop |
    Where-Object { $_.PSChildName -like 'ZW3D WuKong 32.*' } |
    Sort-Object PSChildName -Descending |
    Select-Object -First 1

if (-not $versionKey) {
    throw 'ZW3D WuKong 2027 registry profile was not found.'
}

$pluginKey = Join-Path $versionKey.PSPath 'zh_CN\Plugin_x64\MyFirstPlugin'
New-Item -Path $pluginKey -Force | Out-Null
New-ItemProperty -Path $pluginKey -Name Application -Value 'MyFirstPlugin.dll' -PropertyType String -Force | Out-Null
New-ItemProperty -Path $pluginKey -Name Description -Value '图库管理' -PropertyType String -Force | Out-Null
# ZW3D WuKong 2027 SP1 uses flag 15 for applications that must be loaded
# during main UI startup (the built-in AppMenu registration uses the same
# value).  Flag 7 can defer the DLL until its command is typed, which is too
# late for this plugin's ribbon resources.
New-ItemProperty -Path $pluginKey -Name Load -Value 15 -PropertyType DWord -Force | Out-Null
New-ItemProperty -Path $pluginKey -Name Location -Value $userApiRoot -PropertyType String -Force | Out-Null
New-ItemProperty -Path $pluginKey -Name Resource -Value $pluginResource -PropertyType String -Force | Out-Null

Write-Host "MyFirstPlugin action installed at: $profileResourcePool"
Write-Host "MyFirstPlugin strategies installed for all UI roles under: $profileRoot"
Write-Host "MyFirstPlugin startup registration installed at: $pluginKey"
Write-Host 'Restart ZW3D so the DLL and registered profile module are loaded.'
