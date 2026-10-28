$ErrorActionPreference = 'Stop'
$appProject = 'F:\ESP32\niaio\.worktrees\mobile-app-integration\mobile-app'
$env:JAVA_HOME = 'F:\ESP32\niaio\.toolchains\mobile-v1\jdk\jdk-17.0.20.1+1'
$env:ANDROID_HOME = 'F:\ESP32\niaio\.toolchains\mobile-v1\android-sdk'
$env:ANDROID_SDK_ROOT = $env:ANDROID_HOME
$env:ANDROID_USER_HOME = 'F:\ESP32\niaio\.worktrees\mobile-android\mobile-app\.android-user'
$env:GRADLE_USER_HOME = 'F:\ESP32\niaio\.worktrees\mobile-android\mobile-app\.gradle-cache'
$env:JAVA_TOOL_OPTIONS = '-Djavax.net.ssl.trustStoreType=Windows-ROOT -Djavax.net.ssl.trustStore=NONE'
$env:PATH = "$env:JAVA_HOME\bin;$env:PATH"
Push-Location -LiteralPath $appProject
try {
    & .\gradlew.bat --offline :app:testDebugUnitTest :app:lintDebug :app:assembleDebug
    $appBuildExit = $LASTEXITCODE
    if ($appBuildExit -eq 0) {
        Copy-Item -LiteralPath (Join-Path $appProject 'app/build/outputs/apk/debug/app-debug.apk') -Destination (Join-Path $PSScriptRoot 'niaio-mobile-ui-debug.apk')
        Write-Output (Join-Path $PSScriptRoot 'niaio-mobile-ui-debug.apk')
    }
} finally { Pop-Location }
exit $appBuildExit
