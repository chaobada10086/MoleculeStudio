#requires -Version 5.1
<#
.SYNOPSIS
Sign MoleculeStudio release binaries with Authenticode.
.DESCRIPTION
Modes:
  -Thumbprint <hex>                      existing code-signing cert in CurrentUser\My
  -PfxPath <file> [-PfxPassword <pwd>]   import PFX for this run
  -SelfSigned                            local self-signed cert (testing only; trusted only on machines where installed)
Always SHA-256; RFC3161 timestamp when reachable.
#>
param(
  [Parameter(Mandatory=$true)][string[]]$Files,
  [string]$Thumbprint,
  [string]$PfxPath,
  [string]$PfxPassword,
  [switch]$SelfSigned,
  [string]$TimestampUrl = "http://timestamp.digicert.com",
  [switch]$NoTimestamp
)
$ErrorActionPreference = "Stop"

function Get-SigningCertificate {
  if ($Thumbprint) {
    $cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Thumbprint -eq $Thumbprint }
    if (-not $cert) { throw "Certificate $Thumbprint not found in CurrentUser\My" }
    return $cert
  }
  if ($PfxPath) {
    $secure = if ($PfxPassword) { ConvertTo-SecureString $PfxPassword -AsPlainText -Force } else { $null }
    return Import-PfxCertificate -FilePath $PfxPath -CertStoreLocation Cert:\CurrentUser\My -Password $secure -Exportable
  }
  if ($SelfSigned) {
    $subject = "CN=MoleculeStudio Local Code Signing"
    $cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq $subject -and $_.HasPrivateKey } | Select-Object -First 1
    if (-not $cert) {
      $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject -KeyUsage DigitalSignature -CertStoreLocation Cert:\CurrentUser\My -KeyExportPolicy Exportable -KeyAlgorithm RSA -KeyLength 3072 -HashAlgorithm SHA256 -NotAfter (Get-Date).AddYears(5)
    }
    $cer = [IO.Path]::Combine([IO.Path]::GetTempPath(), $cert.Thumbprint + ".cer")
    Export-Certificate -Cert $cert -FilePath $cer -Force | Out-Null
    foreach ($store in @("Root","TrustedPublisher")) {
      $path = "Cert:\CurrentUser\$store"
      if (-not (Get-ChildItem $path | Where-Object Thumbprint -eq $cert.Thumbprint)) {
        Import-Certificate -FilePath $cer -CertStoreLocation $path | Out-Null
      }
    }
    return $cert
  }
  throw "Specify -Thumbprint, -PfxPath or -SelfSigned"
}

$cert = Get-SigningCertificate
$results = @()
foreach ($file in $Files) {
  if (-not (Test-Path -LiteralPath $file)) { throw "Missing file: $file" }
  $resolved = (Resolve-Path -LiteralPath $file).Path
  $params = @{ FilePath = $resolved; Certificate = $cert; HashAlgorithm = "SHA256" }
  if (-not $NoTimestamp -and $TimestampUrl) { $params.TimestampServer = $TimestampUrl }
  try { Set-AuthenticodeSignature @params | Out-Null }
  catch {
    if ($params.ContainsKey("TimestampServer")) { $params.Remove("TimestampServer"); Set-AuthenticodeSignature @params | Out-Null } else { throw }
  }
  $check = Get-AuthenticodeSignature -LiteralPath $resolved
  $results += [pscustomobject]@{
    File = $resolved
    Status = $check.Status.ToString()
    Signer = $check.SignerCertificate.Subject
    Thumbprint = $cert.Thumbprint
    Timestamped = ($null -ne $check.TimeStamperCertificate)
  }
}
$results | Format-Table -AutoSize | Out-String | Write-Host
if ($results.Status -contains "NotSigned") { exit 1 }
