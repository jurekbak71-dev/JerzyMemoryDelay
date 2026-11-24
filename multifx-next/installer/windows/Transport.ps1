# Windows PowerShell 5.1 compatible; no credentials in command line or log.
Add-Type -TypeDefinition @'
using System.Text;
public static class JerzyArgv {
 public static string Quote(string value) {
  var b=new StringBuilder(); b.Append('"'); int slashes=0;
  foreach(char c in value) {
   if(c=='\\') { slashes++; continue; }
   if(c=='"') { b.Append('\\',slashes*2+1); b.Append('"'); }
   else { b.Append('\\',slashes); b.Append(c); }
   slashes=0;
  }
  b.Append('\\',slashes*2); b.Append('"'); return b.ToString();
 }
}
'@
function Invoke-JerzyTool([string]$File,[string[]]$ToolArgs,[int]$TimeoutSeconds=1800) {
 $info=New-Object System.Diagnostics.ProcessStartInfo
 $info.FileName=$File
 $info.Arguments=(($ToolArgs | ForEach-Object { [JerzyArgv]::Quote($_) }) -join ' ')
 $info.UseShellExecute=$false; $info.CreateNoWindow=$true
 $info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true
 $p=New-Object System.Diagnostics.Process; $p.StartInfo=$info
 try {
  if(-not $p.Start()) { throw 'Nie udalo sie uruchomic narzedzia SSH.' }
  $stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync()
  $clock=[Diagnostics.Stopwatch]::StartNew()
  while(-not $p.WaitForExit(100)) {
   if($script:PumpUi) { & $script:PumpUi }
   if($clock.Elapsed.TotalSeconds -gt $TimeoutSeconds) { $p.Kill();$p.WaitForExit();throw 'Przekroczono czas operacji. Sprawdz polaczenie z RPi.' }
  }
  return @{Code=$p.ExitCode;Out=$stdout.Result;Err=$stderr.Result}
 } finally { $p.Dispose() }
}
function Get-JerzyFingerprint([string]$Text) {
 $match=[regex]::Match($Text,'(?m)^\s*(ssh-(?:ed25519|rsa)|ecdsa-sha2-[a-z0-9-]+)\s+\d+\s+(SHA256:[A-Za-z0-9+/=]+)\s*$')
 if(-not $match.Success) { throw 'Nie odczytano klucza RPi. Polacz Windows z Wi-Fi Jerzy-MultiFX i sprawdz adres.' }
 return $match.Groups[2].Value
}
function ConvertTo-JerzyPythonCommand([string]$Code) {
 $base64=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Code))
 return 'python3 -c "import base64;exec(base64.b64decode('''+$base64+'''))"'
}
