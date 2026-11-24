$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
foreach($file in Get-ChildItem (Join-Path $root 'installer/windows') -Filter '*.ps1') {
 $errors=$null;$tokens=$null
 [void][Management.Automation.Language.Parser]::ParseFile($file.FullName,[ref]$tokens,[ref]$errors)
 if($errors.Count){throw ($errors | Out-String)}
}
. (Join-Path $root 'installer/windows/Transport.ps1')
$pin=Get-JerzyFingerprint "Host key fingerprint is:`nssh-ed25519 255 SHA256:abcdef0123456789+/=`n"
if($pin -ne 'SHA256:abcdef0123456789+/='){throw 'Fingerprint parse'}
$failed=$false
try { Get-JerzyFingerprint 'no SSH key' } catch {$failed=$true}
if(-not $failed){throw 'Missing key accepted'}
$result=Invoke-JerzyTool '/usr/bin/python3' @('-c','import json,sys;print(json.dumps(sys.argv[1:]))','space path','quote"here','end\') 10
if($result.Code -ne 0){throw $result.Err}
$got=$result.Out | ConvertFrom-Json
if($got[0] -ne 'space path' -or $got[1] -ne 'quote"here' -or $got[2] -ne 'end\'){throw 'Argv quoting failed'}
$command=ConvertTo-JerzyPythonCommand 'print("REMOTE_BASE64_OK")'
$result=Invoke-JerzyTool '/bin/sh' @('-c',$command) 10
if($result.Code -ne 0 -or $result.Out.Trim() -ne 'REMOTE_BASE64_OK'){throw 'Remote command encoding failed'}
Write-Output 'PowerShell parser, fingerprint rejection, real process argv, base64 remote command: PASS'
