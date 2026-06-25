$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$tools = Join-Path $root ".build-tools"

$env:JAVA_HOME = Join-Path $tools "jdk-17"
$env:ANDROID_HOME = Join-Path $tools "android-sdk"
$env:ANDROID_SDK_ROOT = $env:ANDROID_HOME
$env:Path = (Join-Path $env:JAVA_HOME "bin") + ";" +
    (Join-Path $tools "gradle-8.4\bin") + ";" +
    (Join-Path $env:ANDROID_HOME "platform-tools") + ";" +
    $env:Path

Push-Location $root
try {
    gradle --no-daemon "-Pandroid.useAndroidX=true" assembleDebug
} finally {
    Pop-Location
}
