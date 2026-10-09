$ErrorActionPreference = 'Stop'
$env:JAVA_HOME = 'F:\ESP32\niaio\.toolchains\mobile-v1\jdk\jdk-17.0.20.1+1'
$env:ANDROID_HOME = 'F:\ESP32\niaio\.toolchains\mobile-v1\android-sdk'
$env:ANDROID_SDK_ROOT = $env:ANDROID_HOME
$env:ANDROID_USER_HOME = 'F:\ESP32\niaio\.worktrees\mobile-android\mobile-app\.android-user'
$env:GRADLE_USER_HOME = 'F:\ESP32\niaio\.worktrees\mobile-android\mobile-app\.gradle-cache'
$env:JAVA_TOOL_OPTIONS = '-Djavax.net.ssl.trustStoreType=Windows-ROOT -Djavax.net.ssl.trustStore=NONE'
$env:PATH = "$env:JAVA_HOME\bin;$env:PATH"
Push-Location -LiteralPath $PSScriptRoot
try {
    & .\gradlew.bat --offline :app:testDebugUnitTest :app:lintDebug :app:assembleDebug
    $result = $LASTEXITCODE
} finally { Pop-Location }
exit $result
